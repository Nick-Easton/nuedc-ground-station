#!/usr/bin/env python3
import math

import rospy

from nuedc_ground_air.msg import Detection2D


def main():
    rospy.init_node("fake_yolo_node")
    pub = rospy.Publisher("/vision/detections", Detection2D, queue_size=10)
    rate = rospy.Rate(5)
    tick = 0

    while not rospy.is_shutdown():
        msg = Detection2D()
        msg.header.stamp = rospy.Time.now()
        msg.class_name = "target"
        msg.confidence = 0.75 + 0.1 * math.sin(tick / 10.0)
        msg.center_x = 320.0 + 80.0 * math.sin(tick / 15.0)
        msg.center_y = 240.0
        msg.width = 80.0
        msg.height = 60.0
        pub.publish(msg)

        tick += 1
        rate.sleep()


if __name__ == "__main__":
    main()

