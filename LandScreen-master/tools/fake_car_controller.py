#!/usr/bin/env python3
"""Pseudo-terminal C07A simulator for the LandScreen car-control page."""

import argparse
import errno
import os
import select
import signal
import struct
import sys
import time

if os.name == "posix":
    import pty
    import tty
else:
    pty = None
    tty = None

FRAME_VERSION = 1
FRAME_IMU = 0x01
FRAME_MOTOR_STATUS = 0x02
FRAME_PING = 0x80
FRAME_PING_ACK = 0x81
FRAME_ARM = 0x82
FRAME_DRIVE = 0x83
FRAME_STOP = 0x84
FRAME_ACK = 0x90
ARM_GUARD = 0xA55A
MAX_COMMAND = 400
WATCHDOG_SECONDS = 0.300


def crc16(data):
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 \
                else (value << 1) & 0xFFFF
    return value


def make_frame(frame_type, payload=b""):
    body = bytes((FRAME_VERSION, frame_type, len(payload))) + payload
    return b"\xAA\x55" + body + struct.pack("<H", crc16(body))


class FrameParser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 7:
            start = self.buffer.find(b"\xAA\x55")
            if start < 0:
                self.buffer[:] = b"\xAA" if self.buffer[-1:] == b"\xAA" else b""
                break
            del self.buffer[:start]
            if len(self.buffer) < 7:
                break
            version, frame_type, size = self.buffer[2:5]
            if version != FRAME_VERSION or size > 64:
                del self.buffer[0]
                continue
            frame_size = size + 7
            if len(self.buffer) < frame_size:
                break
            expected = struct.unpack_from("<H", self.buffer, frame_size - 2)[0]
            actual = crc16(self.buffer[2:frame_size - 2])
            if actual != expected:
                del self.buffer[0]
                continue
            frames.append((frame_type, bytes(self.buffer[5:5 + size])))
            del self.buffer[:frame_size]
        return frames


def newer(sequence, previous):
    difference = (sequence - previous) & 0xFFFF
    return difference != 0 and difference < 0x8000


class FakeController:
    def __init__(self, calibration_seconds=1.0):
        self.started = time.monotonic()
        self.calibration_seconds = calibration_seconds
        self.sequence = 0
        self.timestamp_us = 0
        self.encoder_left = 0
        self.encoder_right = 0
        self.armed = False
        self.watchdog_tripped = False
        self.limited = False
        self.last_command_sequence = 0
        self.target_left = 0
        self.target_right = 0
        self.applied_left = 0
        self.applied_right = 0
        self.last_drive = 0.0
        self.stop_reason = 6

    def ready(self, now):
        return now - self.started >= self.calibration_seconds

    def ack(self, command, sequence, result):
        return make_frame(FRAME_ACK, struct.pack("<BHB", command, sequence, result))

    def handle(self, frame_type, payload, now):
        if frame_type == FRAME_PING and len(payload) == 2:
            return [make_frame(FRAME_PING_ACK, payload)]
        sequence = struct.unpack_from("<H", payload)[0] if len(payload) >= 2 else 0
        if frame_type == FRAME_ARM:
            if len(payload) != 4:
                result = 6
            elif struct.unpack_from("<H", payload, 2)[0] != ARM_GUARD:
                result = 4
            elif not self.ready(now):
                result = 2
            else:
                self.armed = True
                self.watchdog_tripped = False
                self.limited = False
                self.last_command_sequence = sequence
                self.last_drive = now
                self.stop_reason = 0
                result = 0
            return [self.ack(frame_type, sequence, result)]
        if frame_type == FRAME_DRIVE:
            if len(payload) != 6:
                result = 6
            elif not self.armed:
                result = 3
            elif not newer(sequence, self.last_command_sequence):
                result = 5
            else:
                left, right = struct.unpack_from("<hh", payload, 2)
                limited_left = max(-MAX_COMMAND, min(MAX_COMMAND, left))
                limited_right = max(-MAX_COMMAND, min(MAX_COMMAND, right))
                self.limited = left != limited_left or right != limited_right
                self.target_left = limited_left
                self.target_right = limited_right
                self.applied_left = limited_left
                self.applied_right = limited_right
                self.last_command_sequence = sequence
                self.last_drive = now
                result = 1 if self.limited else 0
            return [self.ack(frame_type, sequence, result)]
        if frame_type == FRAME_STOP:
            if len(payload) != 3:
                result = 6
            else:
                self.last_command_sequence = sequence
                self.stop(payload[2])
                result = 0
            return [self.ack(frame_type, sequence, result)]
        return []

    def stop(self, reason):
        self.armed = False
        self.target_left = 0
        self.target_right = 0
        self.applied_left = 0
        self.applied_right = 0
        self.stop_reason = reason

    def tick(self, now):
        if self.armed and now - self.last_drive >= WATCHDOG_SECONDS:
            self.watchdog_tripped = True
            self.stop(4)
        self.encoder_left += int(self.applied_left / 50)
        self.encoder_right += int(self.applied_right / 50)
        self.timestamp_us = (self.timestamp_us + 10000) & 0xFFFFFFFF

    def telemetry_frame(self, now):
        ready = self.ready(now)
        calibration = 100 if ready else min(99, int((now - self.started) * 100 /
                                                     self.calibration_seconds))
        status = 1 << 1 if ready else 1
        payload = struct.pack(
            "<HIhhhhhhhiiHB", self.sequence, self.timestamp_us,
            0, 0, 16384, 0, 0, 0, 0,
            self.encoder_left, self.encoder_right, status, calibration)
        self.sequence = (self.sequence + 1) & 0xFFFF
        return make_frame(FRAME_IMU, payload)

    def motor_status_frame(self, now):
        bits = (1 if self.ready(now) else 0) | (2 if self.armed else 0)
        if self.watchdog_tripped:
            bits |= 4
        if self.limited:
            bits |= 8
        remaining = max(0, int((WATCHDOG_SECONDS - (now - self.last_drive)) * 1000)) \
            if self.armed else 0
        payload = struct.pack(
            "<HHhhhhHBB", bits, self.last_command_sequence,
            self.target_left, self.target_right,
            self.applied_left, self.applied_right,
            remaining, self.stop_reason, 40)
        return make_frame(FRAME_MOTOR_STATUS, payload)


def write_all(fd, data):
    view = memoryview(data)
    while view:
        try:
            written = os.write(fd, view)
            view = view[written:]
        except BlockingIOError:
            select.select([], [fd], [], 0.1)


def main():
    if os.name != "posix":
        raise RuntimeError("the pseudo-terminal simulator requires Linux")
    parser = argparse.ArgumentParser()
    parser.add_argument("--link", default="/tmp/c07a-sim",
                        help="optional symlink created for the pseudo-terminal")
    parser.add_argument("--calibration-seconds", type=float, default=1.0)
    args = parser.parse_args()

    master_fd, slave_fd = pty.openpty()
    tty.setraw(slave_fd)
    os.set_blocking(master_fd, False)
    slave_path = os.ttyname(slave_fd)
    if args.link:
        if os.path.lexists(args.link):
            if not os.path.islink(args.link):
                raise RuntimeError("refusing to replace non-symlink: {}".format(args.link))
            os.unlink(args.link)
        os.symlink(slave_path, args.link)

    print("C07A simulator ready: {}".format(args.link or slave_path), flush=True)
    print("Run: CAR_SERIAL_PORT={} ./planescreen".format(args.link or slave_path),
          flush=True)

    running = True

    def stop_running(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop_running)
    signal.signal(signal.SIGTERM, stop_running)
    controller = FakeController(max(0.05, args.calibration_seconds))
    frame_parser = FrameParser()
    next_telemetry = time.monotonic()
    next_motor_status = next_telemetry
    try:
        while running:
            now = time.monotonic()
            readable, _, _ = select.select([master_fd], [], [], 0.005)
            if readable:
                try:
                    incoming = os.read(master_fd, 1024)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    incoming = b""
                for frame_type, payload in frame_parser.feed(incoming):
                    for response in controller.handle(frame_type, payload, now):
                        write_all(master_fd, response)
            while now >= next_telemetry:
                controller.tick(now)
                write_all(master_fd, controller.telemetry_frame(now))
                next_telemetry += 0.010
            if now >= next_motor_status:
                write_all(master_fd, controller.motor_status_frame(now))
                next_motor_status += 0.100
    finally:
        os.close(master_fd)
        os.close(slave_fd)
        if args.link and os.path.islink(args.link) and os.readlink(args.link) == slave_path:
            os.unlink(args.link)


if __name__ == "__main__":
    sys.exit(main())
