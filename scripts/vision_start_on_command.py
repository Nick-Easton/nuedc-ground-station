#!/usr/bin/env python3
"""Start the USB camera and TensorRT YOLO only after a mission START command."""

import os
import shlex
import signal
import subprocess

import rospy
import rosnode

from nuedc_ground_air.msg import MissionCommand


class VisionStartOnCommand:
    def __init__(self):
        self.camera = rospy.get_param("~camera", "/dev/video0")
        self.show_window = rospy.get_param("~show_window", False)
        self.yolo_setup = os.path.expanduser(
            rospy.get_param(
                "~yolo_setup",
                "~/catkin_ws_yolo_trt/devel/setup.bash",
            )
        )
        self.app_setup = os.path.expanduser(
            rospy.get_param("~app_setup", "~/catkin_ws/devel/setup.bash")
        )
        self.app_source = os.path.expanduser(
            rospy.get_param("~app_source", "~/catkin_ws/src")
        )
        self.process = None
        rospy.Subscriber("/mission/command", MissionCommand, self.on_command)
        rospy.loginfo("Vision starter armed; camera will start after START.")

    def on_command(self, msg):
        command_name = msg.command.strip().upper()
        if command_name in ("STOP", "ABORT", "FINISH", "FINISHED", "COMPLETED", "LANDED"):
            self.stop_vision()
            return
        if command_name != "START":
            return
        if self.process and self.process.poll() is None:
            rospy.loginfo("Vision pipeline is already running.")
            return
        try:
            if "/yolo_trt_node" in rosnode.get_node_names():
                rospy.loginfo("Vision pipeline is already running in ROS.")
                return
        except Exception as exc:
            rospy.logwarn("Could not query existing ROS nodes: %s", exc)
        if not os.path.exists(self.camera):
            rospy.logerr("Cannot start vision: camera device does not exist: %s", self.camera)
            return
        if not os.path.exists(self.yolo_setup):
            rospy.logerr("Cannot start vision: YOLO workspace setup does not exist: %s", self.yolo_setup)
            return
        if not os.path.exists(self.app_setup):
            rospy.logerr("Cannot start vision: application workspace setup does not exist: %s", self.app_setup)
            return
        if not os.path.isdir(self.app_source):
            rospy.logerr("Cannot start vision: application source path does not exist: %s", self.app_source)
            return

        launch_command = " ".join(
            [
                "source /opt/ros/noetic/setup.bash &&",
                "source {} &&".format(shlex.quote(self.app_setup)),
                "source {} &&".format(shlex.quote(self.yolo_setup)),
                "export ROS_PACKAGE_PATH={}:$ROS_PACKAGE_PATH &&".format(
                    shlex.quote(self.app_source)
                ),
                "exec roslaunch nuedc_ground_air onboard_real_vision_demo.launch",
                "camera:={}".format(shlex.quote(self.camera)),
                "show_window:={}".format("true" if self.show_window else "false"),
            ]
        )
        command = ["bash", "-lc", launch_command]
        self.process = subprocess.Popen(command, start_new_session=True)
        rospy.loginfo("Started camera and TensorRT YOLO after mission START.")

    def stop_vision(self):
        if self.process and self.process.poll() is None:
            try:
                os.killpg(os.getpgid(self.process.pid), signal.SIGINT)
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
        else:
            try:
                rosnode.kill_nodes(
                    [
                        "/usb_cam",
                        "/yolo_trt_node",
                        "/vision_detection_adapter",
                        "/grid_recognition_state_machine",
                    ]
                )
            except Exception as exc:
                rospy.logwarn("Could not stop orphaned vision nodes: %s", exc)
        self.process = None
        rospy.loginfo("Stopped camera and TensorRT YOLO after mission STOP.")


if __name__ == "__main__":
    rospy.init_node("vision_start_on_command")
    VisionStartOnCommand()
    rospy.spin()
