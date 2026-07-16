#!/usr/bin/env python3
import json
from collections import Counter

import rospy
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
        self.min_confidence = float(rospy.get_param("~min_confidence", 0.60))
        self.max_rate = float(rospy.get_param("~max_rate", 5.0))
        self.max_detections = int(rospy.get_param("~max_detections", 10))
        self.class_names = rospy.get_param(
            "~class_names", ["elephant", "tiger", "monkey", "kongque", "wolf"]
        )
        self.last_publish_time = None
        self.last_summary = None

        self.publisher = rospy.Publisher(self.output_topic, Detection2D, queue_size=10)
        self.summary_publisher = rospy.Publisher(
            self.summary_topic, String, queue_size=1, latch=True
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
        for output in forwarded:
            self.publisher.publish(output)

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
