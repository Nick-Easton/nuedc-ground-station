#!/usr/bin/env python3
"""Record detections once per grid without controlling flight or navigation."""

import json
import re
from collections import Counter, defaultdict

import rospy
from std_msgs.msg import String

from nuedc_ground_air.msg import Detection2D, MissionCommand


VALID_CLASSES = ("elephant", "tiger", "monkey", "kongque", "wolf")
GRID_PATTERN = re.compile(r"^A([1-9])\s*,?\s*B([1-7])$", re.IGNORECASE)


class GridRecognitionStateMachine:
    def __init__(self):
        self.stabilize_seconds = float(rospy.get_param("~stabilize_seconds", 0.3))
        self.recognition_seconds = float(rospy.get_param("~recognition_seconds", 1.0))
        self.min_confidence = float(rospy.get_param("~min_confidence", 0.60))
        self.min_confirmation_frames = max(
            1, int(rospy.get_param("~min_confirmation_frames", 3))
        )
        self.current_grid_topic = rospy.get_param("~current_grid_topic", "/current_grid")
        self.detection_topic = rospy.get_param("~detection_topic", "/vision/detections")

        self.armed = False
        self.state = "WAIT_START"
        self.current_grid = None
        self.state_started = rospy.Time.now()
        self.scanned_grids = set()
        self.frame_counts = defaultdict(Counter)

        self.result_pub = rospy.Publisher(
            "/vision/grid_result", String, queue_size=10, latch=True
        )
        self.state_pub = rospy.Publisher("/vision/grid_state", String, queue_size=10, latch=True)
        rospy.Subscriber("/mission/command", MissionCommand, self.on_command)
        rospy.Subscriber(self.current_grid_topic, String, self.on_current_grid)
        rospy.Subscriber(self.detection_topic, Detection2D, self.on_detection)
        self.timer = rospy.Timer(rospy.Duration(0.05), self.on_timer)
        self.publish_state("waiting for mission START")
        rospy.loginfo(
            "Grid recognition interface ready: grid_topic=%s detection_topic=%s",
            self.current_grid_topic,
            self.detection_topic,
        )

    def publish_state(self, detail):
        payload = {
            "state": self.state,
            "grid": self.current_grid or "",
            "detail": detail,
            "scanned": sorted(self.scanned_grids),
        }
        self.state_pub.publish(String(data=json.dumps(payload, ensure_ascii=False)))

    def on_command(self, msg):
        command = msg.command.strip().upper()
        if command == "START":
            self.armed = True
            self.state = "WAIT_GRID"
            self.current_grid = None
            self.scanned_grids.clear()
            self.frame_counts.clear()
            self.state_started = rospy.Time.now()
            self.publish_state("recognition armed; waiting for /current_grid")
        elif command in ("STOP", "ABORT"):
            self.armed = False
            self.state = "WAIT_START"
            self.current_grid = None
            self.frame_counts.clear()
            self.publish_state("recognition stopped")

    def on_current_grid(self, msg):
        match = GRID_PATTERN.match(msg.data.strip())
        if not match:
            rospy.logwarn_throttle(2.0, "Invalid /current_grid value: %s", msg.data)
            return
        grid = "A{}B{}".format(match.group(1), match.group(2))
        if not self.armed:
            self.publish_state("grid received, but recognition is not armed")
            return
        if grid == self.current_grid:
            return

        if self.state == "RECORDING" and self.current_grid:
            self.finalize_grid("grid changed before time window ended")

        self.current_grid = grid
        self.frame_counts.clear()
        self.state_started = rospy.Time.now()
        if grid in self.scanned_grids:
            self.state = "GRID_ALREADY_SCANNED"
            self.publish_state("grid already scanned; no duplicate record")
            return

        self.state = "STABILIZING"
        self.publish_state("waiting for stable hover")

    def on_detection(self, msg):
        if self.state != "RECORDING" or msg.confidence < self.min_confidence:
            return
        class_name = msg.class_name.strip().lower()
        if class_name not in VALID_CLASSES:
            return
        frame_key = msg.header.stamp.to_nsec() or msg.header.seq
        self.frame_counts[frame_key][class_name] += 1

    def on_timer(self, _event):
        elapsed = (rospy.Time.now() - self.state_started).to_sec()
        if self.state == "STABILIZING" and elapsed >= self.stabilize_seconds:
            self.state = "RECORDING"
            self.state_started = rospy.Time.now()
            self.frame_counts.clear()
            self.publish_state("collecting detections for this grid")
        elif self.state == "RECORDING" and elapsed >= self.recognition_seconds:
            self.finalize_grid("recognition window completed")

    def finalize_grid(self, detail):
        if not self.current_grid:
            return
        counts = {class_name: 0 for class_name in VALID_CLASSES}
        for class_name in VALID_CLASSES:
            supported_counts = sorted(
                frame[class_name]
                for frame in self.frame_counts.values()
                if frame[class_name] > 0
            )
            if len(supported_counts) < self.min_confirmation_frames:
                continue
            counts[class_name] = supported_counts[(len(supported_counts) - 1) // 2]

        match = GRID_PATTERN.match(self.current_grid)
        payload = {
            "grid": self.current_grid,
            "a": int(match.group(1)),
            "b": int(match.group(2)),
            "counts": counts,
            "sampled_frames": len(self.frame_counts),
        }
        self.result_pub.publish(String(data=json.dumps(payload, separators=(",", ":"))))
        self.scanned_grids.add(self.current_grid)
        self.state = "GRID_COMPLETE"
        self.frame_counts.clear()
        self.publish_state(detail)
        rospy.loginfo("Grid recognition complete: %s", payload)


if __name__ == "__main__":
    rospy.init_node("grid_recognition_state_machine")
    GridRecognitionStateMachine()
    rospy.spin()
