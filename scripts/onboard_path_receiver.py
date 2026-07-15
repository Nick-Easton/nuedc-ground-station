#!/usr/bin/env python3
import json
import math

import rospy
from nav_msgs.msg import Path
from std_msgs.msg import String


class OnboardPathReceiver:
    def __init__(self):
        self.max_points = int(rospy.get_param("~max_points", 500))
        self.ack_pub = rospy.Publisher("/planner/path_ack", String, queue_size=10, latch=True)
        rospy.Subscriber("/planner/path", Path, self.on_path)
        rospy.loginfo("Onboard path receiver ready")

    def on_path(self, msg):
        accepted, reason = self.validate(msg)
        ack = {
            "accepted": accepted,
            "points": len(msg.poses),
            "frame_id": msg.header.frame_id,
            "reason": reason,
        }
        self.ack_pub.publish(String(data=json.dumps(ack, separators=(",", ":"))))

        if accepted:
            rospy.loginfo("Accepted ground path: %d points", len(msg.poses))
        else:
            rospy.logwarn("Rejected ground path: %s", reason)

    def validate(self, msg):
        if msg.header.frame_id != "map":
            return False, "frame_id must be map"
        if not msg.poses:
            return False, "path is empty"
        if len(msg.poses) > self.max_points:
            return False, "too many points"

        for pose in msg.poses:
            position = pose.pose.position
            values = (position.x, position.y, position.z)
            if not all(math.isfinite(value) for value in values):
                return False, "path contains a non-finite coordinate"
        return True, "ok"


if __name__ == "__main__":
    rospy.init_node("onboard_path_receiver")
    OnboardPathReceiver()
    rospy.spin()
