#!/usr/bin/env python3
"""Offline TCP source for visually checking the read-only ground-air monitor."""

import argparse
import json
import math
import socketserver
import threading
import time


TRACK_LENGTH = 3.0 + 2.0 * math.pi * 0.75


def car_pose(distance):
    distance %= TRACK_LENGTH
    if distance <= 1.5:
        return 1.5, 2.0 + distance, 90.0
    if distance <= 1.5 + math.pi * 0.75:
        theta = math.pi - (distance - 1.5) / 0.75
        return (
            2.25 + 0.75 * math.cos(theta),
            3.5 + 0.75 * math.sin(theta),
            math.degrees(theta - math.pi / 2.0),
        )
    if distance <= 3.0 + math.pi * 0.75:
        segment = distance - (1.5 + math.pi * 0.75)
        return 3.0, 3.5 - segment, -90.0
    theta = -(distance - (3.0 + math.pi * 0.75)) / 0.75
    return (
        2.25 + 0.75 * math.cos(theta),
        2.0 + 0.75 * math.sin(theta),
        math.degrees(theta - math.pi / 2.0),
    )


def mission_state(elapsed):
    if elapsed < 4.0:
        return "TAKEOFF"
    if elapsed < 7.0:
        return "HOVER_3S"
    if elapsed < 10.0:
        return "ACQUIRE_CAR"
    if elapsed < 46.0:
        return "FOLLOW_CAR"
    if elapsed < 51.0:
        return "ALIGN_AND_DROP"
    if elapsed < 58.0:
        return "DROP"
    if elapsed < 72.0:
        return "RETURN_HOME"
    if elapsed < 84.0:
        return "LANDING"
    return "COMPLETE"


def build_payload(elapsed):
    elapsed %= 86.0
    x, y, yaw = car_pose(max(0.0, elapsed - 4.0) * 0.12)
    state = mission_state(elapsed)
    target_visible = 8.0 < elapsed < 58.0
    altitude = min(1.5, elapsed / 4.0 * 1.5)
    if elapsed > 72.0:
        altitude = max(0.0, 1.5 - (elapsed - 72.0) * 0.12)
    return {
        "type": "ground_air_telemetry",
        "timestamp_ms": int(time.time() * 1000),
        "source": "lidar_tcp_demo",
        "latency_ms": 18,
        "car": {
            "valid": True,
            "x_m": round(x, 4),
            "y_m": round(y, 4),
            "yaw_deg": round(yaw, 2),
            "speed_mps": 0.0 if elapsed < 4.0 else 0.12,
            "battery_percent": round(86.0 - elapsed * 0.08, 1),
        },
        "drone": {
            "valid": True,
            "x_m": round(0.75 if elapsed < 4.0 else x - 0.10, 4),
            "y_m": round(0.75 if elapsed < 4.0 else y - 0.05, 4),
            "z_m": round(altitude, 3),
            "yaw_deg": round(yaw, 2),
            "speed_mps": 0.12 if 10.0 < elapsed < 58.0 else 0.0,
            "battery_percent": round(93.0 - elapsed * 0.16, 1),
            "armed": 0.5 < elapsed < 84.0,
            "flight_mode": "OFFBOARD",
            "target_visible": target_visible,
            "target_confidence": 0.95 if target_visible else 0.0,
        },
        "mission": {
            "mission_id": "D-TCP-DEMO-01",
            "mode": "DROP",
            "state": state,
            "elapsed_s": round(elapsed, 2),
            "drop_done": elapsed >= 51.0,
            "touchdown_confirmed": False,
        },
        "links": {
            "localization_ok": True,
            "car_link_ok": True,
            "drone_link_ok": True,
        },
    }


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        started = time.monotonic()
        while True:
            payload = build_payload(time.monotonic() - started)
            wire = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
            try:
                self.request.sendall(wire)
            except OSError:
                return
            time.sleep(0.1)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8001)
    args = parser.parse_args()
    with Server((args.host, args.port), Handler) as server:
        print("ground-air demo server listening on {}:{}".format(args.host, args.port))
        server.serve_forever()
