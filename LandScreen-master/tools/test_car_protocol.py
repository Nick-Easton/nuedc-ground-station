#!/usr/bin/env python3
import os
import struct
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from fake_car_controller import (  # noqa: E402
    ARM_GUARD,
    FRAME_ACK,
    FRAME_ARM,
    FRAME_DRIVE,
    FrameParser,
    FakeController,
    make_frame,
)


class CarProtocolTest(unittest.TestCase):
    def test_fragmented_crc_frame_is_reassembled(self):
        parser = FrameParser()
        frame = make_frame(FRAME_DRIVE, struct.pack("<Hhh", 8, 120, -90))
        self.assertEqual(parser.feed(b"noise" + frame[:4]), [])
        self.assertEqual(parser.feed(frame[4:]),
                         [(FRAME_DRIVE, struct.pack("<Hhh", 8, 120, -90))])

    def test_arm_drive_limit_and_watchdog(self):
        controller = FakeController(calibration_seconds=0.05)
        now = controller.started + 0.06
        arm = struct.pack("<HH", 10, ARM_GUARD)
        responses = controller.handle(FRAME_ARM, arm, now)
        self.assertEqual(FrameParser().feed(responses[0])[0][0], FRAME_ACK)
        self.assertTrue(controller.armed)

        controller.handle(FRAME_DRIVE, struct.pack("<Hhh", 11, 900, -900), now)
        self.assertEqual((controller.target_left, controller.target_right), (400, -400))
        self.assertTrue(controller.limited)

        controller.tick(now + 0.301)
        self.assertFalse(controller.armed)
        self.assertTrue(controller.watchdog_tripped)
        self.assertEqual(controller.stop_reason, 4)


if __name__ == "__main__":
    unittest.main()
