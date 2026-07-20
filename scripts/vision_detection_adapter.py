#!/usr/bin/env python3
import json
from collections import Counter, deque

import rospy
from geometry_msgs.msg import PoseStamped
from std_msgs.msg import String
from vision_msgs.msg import Detection2DArray

from nuedc_ground_air.msg import Detection2D


class VisionDetectionAdapter:
    def __init__(self):
        self.input_topic = rospy.get_param(
            "~input_topic", "/yolo_trt_node/detections"
        )
        self.output_topic = rospy.get_param("~output_topic", "/vision/detections")
        self.summary_topic = rospy.get_param("~summary_topic", "/vision/summary")
        self.vision_goal_topic = rospy.get_param(
            "~vision_goal_topic", "/mission/vision_goal"
        )
        self.min_confidence = float(rospy.get_param("~min_confidence", 0.60))
        self.max_rate = float(rospy.get_param("~max_rate", 5.0))
        self.max_detections = int(rospy.get_param("~max_detections", 10))
        self.image_width = float(rospy.get_param("~image_width", 640.0))
        self.image_height = float(rospy.get_param("~image_height", 480.0))
        self.max_forward_step = float(
            rospy.get_param("~vision_goal_max_forward_step", 0.20)
        )
        self.max_lateral_step = float(
            rospy.get_param("~vision_goal_max_lateral_step", 0.10)
        )
        self.center_deadband_px = float(
            rospy.get_param("~vision_goal_center_deadband_px", 20.0)
        )
        self.confirmation_window_frames = max(
            1, int(rospy.get_param("~vision_goal_confirmation_window_frames", 5))
        )
        self.confirmation_min_frames = max(
            1, int(rospy.get_param("~vision_goal_confirmation_min_frames", 3))
        )
        self.confirmation_min_frames = min(
            self.confirmation_min_frames, self.confirmation_window_frames
        )
        self.confirmation_center_distance_px = max(
            0.0,
            float(
                rospy.get_param(
                    "~vision_goal_confirmation_center_distance_px", 40.0
                )
            ),
        )
        self.class_names = rospy.get_param(
            "~class_names", ["elephant", "tiger", "monkey", "kongque", "wolf"]
        )
        self.last_publish_time = None
        self.last_summary = None
        self.recent_detection_frames = deque(
            maxlen=self.confirmation_window_frames
        )

        self.publisher = rospy.Publisher(self.output_topic, Detection2D, queue_size=10)
        self.summary_publisher = rospy.Publisher(
            self.summary_topic, String, queue_size=1, latch=True
        )
        self.vision_goal_publisher = rospy.Publisher(
            self.vision_goal_topic, PoseStamped, queue_size=10
        )
        rospy.Subscriber(
            self.input_topic, Detection2DArray, self.on_detections, queue_size=1
        )
        rospy.loginfo(
            "Vision adapter ready: %s -> %s", self.input_topic, self.output_topic
        )

    def on_detections(self, msg):
        now = rospy.Time.now()
        if self.max_rate > 0.0 and self.last_publish_time is not None:
            min_interval = 1.0 / self.max_rate
            if (now - self.last_publish_time).to_sec() < min_interval:
                return

        converted = []
        for detection in msg.detections:
            if not detection.results:
                continue

            hypothesis = max(detection.results, key=lambda result: result.score)
            if hypothesis.score < self.min_confidence:
                continue

            output = Detection2D()
            output.header = msg.header
            output.class_name = self.class_name(hypothesis.id)
            output.confidence = hypothesis.score
            output.center_x = detection.bbox.center.x
            output.center_y = detection.bbox.center.y
            output.width = detection.bbox.size_x
            output.height = detection.bbox.size_y
            converted.append(output)

        converted.sort(key=lambda detection: detection.confidence, reverse=True)
        forwarded = converted[: max(0, self.max_detections)]
        self.recent_detection_frames.append(forwarded)
        for output in forwarded:
            self.publisher.publish(output)
            if self.is_temporally_confirmed(output):
                self.publish_vision_goal(output, now)

        counts = Counter(output.class_name for output in forwarded)
        summary = json.dumps(
            {"total": len(forwarded), "counts": dict(sorted(counts.items()))},
            separators=(",", ":"),
            sort_keys=True,
        )
        if summary != self.last_summary:
            self.summary_publisher.publish(String(data=summary))
            self.last_summary = summary

        self.last_publish_time = now
        if forwarded:
            rospy.loginfo_throttle(
                2.0,
                "Forwarded %d YOLO detection(s) to %s",
                len(forwarded),
                self.output_topic,
            )

    def is_temporally_confirmed(self, candidate):
        max_distance_squared = self.confirmation_center_distance_px ** 2
        matching_frames = 0
        for frame in self.recent_detection_frames:
            for detection in frame:
                if detection.class_name != candidate.class_name:
                    continue
                dx = detection.center_x - candidate.center_x
                dy = detection.center_y - candidate.center_y
                if dx * dx + dy * dy <= max_distance_squared:
                    matching_frames += 1
                    break
        return matching_frames >= self.confirmation_min_frames

    def publish_vision_goal(self, detection, stamp):
        half_width = max(self.image_width / 2.0, 1.0)
        half_height = max(self.image_height / 2.0, 1.0)
        horizontal_error = half_width - detection.center_x
        vertical_error = half_height - detection.center_y

        if abs(horizontal_error) <= self.center_deadband_px:
            horizontal_error = 0.0
        if abs(vertical_error) <= self.center_deadband_px:
            vertical_error = 0.0

        forward_ratio = max(-1.0, min(1.0, vertical_error / half_height))
        lateral_ratio = max(-1.0, min(1.0, horizontal_error / half_width))

        goal = PoseStamped()
        goal.header.stamp = stamp
        goal.header.frame_id = "base_link"
        goal.pose.position.x = forward_ratio * self.max_forward_step
        goal.pose.position.y = lateral_ratio * self.max_lateral_step
        goal.pose.position.z = 0.0
        goal.pose.orientation.x = 0.0
        goal.pose.orientation.y = 0.0
        goal.pose.orientation.z = 0.0
        goal.pose.orientation.w = 1.0
        self.vision_goal_publisher.publish(goal)

    def class_name(self, class_id):
        if isinstance(self.class_names, dict):
            return str(
                self.class_names.get(
                    str(class_id),
                    self.class_names.get(class_id, "class_{}".format(class_id)),
                )
            )
        if 0 <= class_id < len(self.class_names):
            return str(self.class_names[class_id])
        return "class_{}".format(class_id)


if __name__ == "__main__":
    rospy.init_node("vision_detection_adapter")
    VisionDetectionAdapter()
    rospy.spin()
