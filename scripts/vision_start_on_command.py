#!/usr/bin/env python3
"""Manage the camera and TensorRT YOLO process from mission commands."""

import os
import shlex
import signal
import subprocess

import rosnode
import rospy

from nuedc_ground_air.msg import MissionCommand


STOP_COMMANDS = {
    "STOP",
    "ABORT",
    "FINISH",
    "FINISHED",
    "COMPLETE",
    "COMPLETED",
    "LANDED",
    "FAILED",
    "ERROR",
}


def as_bool(value):
    if isinstance(value, str):
        return value.strip().lower() in ("1", "true", "yes", "on")
    return bool(value)


class VisionStartOnCommand:
    def __init__(self):
        self.camera = rospy.get_param("~camera", "/dev/video0")
        self.show_window = as_bool(rospy.get_param("~show_window", False))
        self.confidence = float(rospy.get_param("~confidence", 0.60))
        self.publish_vision_goal = as_bool(
            rospy.get_param("~publish_vision_goal", False)
        )
        self.vision_goal_cooldown_seconds = max(
            0.0, float(rospy.get_param("~vision_goal_cooldown_seconds", 2.0))
        )
        self.vision_goal_dedup_distance_px = max(
            0.0, float(rospy.get_param("~vision_goal_dedup_distance_px", 60.0))
        )
        self.yolo_setup = os.path.expanduser(
            rospy.get_param(
                "~yolo_setup", "~/catkin_ws_yolo_trt/devel/setup.bash"
            )
        )
        self.app_setup = os.path.expanduser(
            rospy.get_param("~app_setup", "~/catkin_ws/devel/setup.bash")
        )
        self.process = None
        rospy.Subscriber("/mission/command", MissionCommand, self.on_command)
        rospy.on_shutdown(lambda: self.stop_vision(stop_orphans=False))
        rospy.loginfo("Vision starter armed; camera will start after START.")

    def on_command(self, msg):
        command_name = msg.command.strip().upper()
        if command_name in STOP_COMMANDS:
            self.stop_vision()
            return
        if command_name != "START":
            return
        self.start_vision()

    def start_vision(self):
        if self.process and self.process.poll() is None:
            rospy.loginfo("Vision pipeline is already running.")
            return

        try:
            if "/yolo_trt_node" in rosnode.get_node_names():
                rospy.loginfo("Vision pipeline is already running in ROS.")
                return
        except Exception as exc:
            rospy.logwarn("Could not query existing ROS nodes: %s", exc)

        for required_path, description in (
            (self.camera, "camera device"),
            (self.yolo_setup, "YOLO workspace setup"),
            (self.app_setup, "application workspace setup"),
        ):
            if not os.path.exists(required_path):
                rospy.logerr(
                    "Cannot start vision: %s does not exist: %s",
                    description,
                    required_path,
                )
                return

        launch_command = " ".join(
            [
                "source /opt/ros/noetic/setup.bash &&",
                "source {} &&".format(shlex.quote(self.yolo_setup)),
                "source {} &&".format(shlex.quote(self.app_setup)),
                "exec roslaunch nuedc_ground_air onboard_real_vision_demo.launch",
                "camera:={}".format(shlex.quote(self.camera)),
                "show_window:={}".format("true" if self.show_window else "false"),
                "confidence:={}".format(self.confidence),
                "publish_vision_goal:={}".format(
                    "true" if self.publish_vision_goal else "false"
                ),
                "vision_goal_cooldown_seconds:={}".format(
                    self.vision_goal_cooldown_seconds
                ),
                "vision_goal_dedup_distance_px:={}".format(
                    self.vision_goal_dedup_distance_px
                ),
                "start_grid_recognition:=false",
            ]
        )
        self.process = subprocess.Popen(
            ["bash", "-lc", launch_command], start_new_session=True
        )
        rospy.loginfo("Started camera and TensorRT YOLO after mission START.")

    def stop_vision(self, stop_orphans=True):
        if self.process and self.process.poll() is None:
            process_group = os.getpgid(self.process.pid)
            try:
                os.killpg(process_group, signal.SIGINT)
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process_group, signal.SIGTERM)
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process_group, signal.SIGKILL)
            except ProcessLookupError:
                pass
        elif stop_orphans:
            try:
                rosnode.kill_nodes(
                    [
                        "/usb_cam",
                        "/yolo_trt_node",
                        "/vision_detection_adapter",
                    ]
                )
            except Exception as exc:
                rospy.logwarn("Could not stop orphaned vision nodes: %s", exc)

        self.process = None
        if stop_orphans:
            rospy.loginfo("Stopped camera and TensorRT YOLO after mission STOP.")


if __name__ == "__main__":
    rospy.init_node("vision_start_on_command")
    VisionStartOnCommand()
    rospy.spin()
