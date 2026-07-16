#!/usr/bin/env python3
"""Start the USB camera and TensorRT YOLO only after a mission START command."""

import os
import subprocess

import rospy

from nuedc_ground_air.msg import MissionCommand


class VisionStartOnCommand:
    def __init__(self):
        self.camera = rospy.get_param("~camera", "/dev/video0")
        self.show_window = rospy.get_param("~show_window", False)
        self.process = None
        rospy.Subscriber("/mission/command", MissionCommand, self.on_command)
        rospy.loginfo("Vision starter armed; camera will start after START.")

    def on_command(self, msg):
        if msg.command.strip().upper() != "START":
            return
        if self.process and self.process.poll() is None:
            rospy.loginfo("Vision pipeline is already running.")
            return
        if not os.path.exists(self.camera):
            rospy.logerr("Cannot start vision: camera device does not exist: %s", self.camera)
            return

        command = [
            "roslaunch", "yolo_trt_ros", "yolo_trt_usb_cam.launch",
            "camera:=" + self.camera,
            "show_window:=" + ("true" if self.show_window else "false"),
        ]
        self.process = subprocess.Popen(command, start_new_session=True)
        rospy.loginfo("Started camera and TensorRT YOLO after mission START.")


if __name__ == "__main__":
    rospy.init_node("vision_start_on_command")
    VisionStartOnCommand()
    rospy.spin()
