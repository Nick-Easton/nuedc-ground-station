#!/usr/bin/env python3
import math

import rospy
from geometry_msgs.msg import PoseStamped
from nav_msgs.msg import Path

from nuedc_ground_air.msg import ForbiddenZones


class GroundPathPlanner:
    def __init__(self):
        self.altitude = float(rospy.get_param("~altitude", 2.0))
        self.spacing = float(rospy.get_param("~grid_spacing", 0.5))
        self.path_pub = rospy.Publisher("/planner/path", Path, queue_size=1, latch=True)
        rospy.Subscriber("/mission/forbidden_zones", ForbiddenZones, self.on_zones)
        rospy.loginfo("Ground planner ready; waiting for forbidden zones")

    def on_zones(self, msg):
        forbidden = {
            (msg.f1x, msg.f1y),
            (msg.f2x, msg.f2y),
            (msg.f3x, msg.f3y),
        }
        forbidden.discard((-1, -1))

        cells = []
        for b in range(1, 8):
            a_values = range(1, 10) if b % 2 == 1 else range(9, 0, -1)
            for a in a_values:
                if (a, b) not in forbidden:
                    cells.append((a, b))

        path = Path()
        path.header.stamp = rospy.Time.now()
        path.header.frame_id = "map"

        for index, (a, b) in enumerate(cells):
            pose = PoseStamped()
            pose.header = path.header
            pose.pose.position.x = (b - 1) * self.spacing
            pose.pose.position.y = (9 - a) * self.spacing
            pose.pose.position.z = self.altitude

            next_index = min(index + 1, len(cells) - 1)
            next_a, next_b = cells[next_index]
            next_x = (next_b - 1) * self.spacing
            next_y = (9 - next_a) * self.spacing
            yaw = math.atan2(next_y - pose.pose.position.y, next_x - pose.pose.position.x)
            pose.pose.orientation.z = math.sin(yaw / 2.0)
            pose.pose.orientation.w = math.cos(yaw / 2.0)
            path.poses.append(pose)

        self.path_pub.publish(path)
        rospy.loginfo(
            "Published ground path: %d points, forbidden=%s",
            len(path.poses),
            sorted(forbidden),
        )


if __name__ == "__main__":
    rospy.init_node("ground_path_planner")
    GroundPathPlanner()
    rospy.spin()
