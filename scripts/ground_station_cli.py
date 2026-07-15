#!/usr/bin/env python3
import sys
import threading

import rospy

from nuedc_ground_air.msg import Detection2D, MissionCommand, MissionState


class GroundStationCli:
    def __init__(self):
        self.command_pub = rospy.Publisher("/mission/command", MissionCommand, queue_size=10)
        rospy.Subscriber("/mission/state", MissionState, self.on_mission_state)
        rospy.Subscriber("/vision/detections", Detection2D, self.on_detection)

    def on_mission_state(self, msg):
        rospy.loginfo("[MISSION] %s | %s | seq=%d", msg.state, msg.detail, msg.command_seq)

    def on_detection(self, msg):
        rospy.loginfo(
            "[DETECTION] %s conf=%.2f center=(%.1f, %.1f)",
            msg.class_name,
            msg.confidence,
            msg.center_x,
            msg.center_y,
        )

    def publish_command(self, command):
        msg = MissionCommand()
        msg.header.stamp = rospy.Time.now()
        msg.command = command.upper()
        self.command_pub.publish(msg)
        rospy.loginfo("Command sent: %s", msg.command)


def input_loop(node):
    print("Ground station ready. Commands: start, stop, abort, quit")
    while not rospy.is_shutdown():
        try:
            command = input("> ").strip().lower()
        except EOFError:
            return

        if command == "quit":
            rospy.signal_shutdown("ground station quit")
            return
        if command in ("start", "stop", "abort"):
            node.publish_command(command)
        elif command:
            print("Unknown command. Use: start, stop, abort, quit")


if __name__ == "__main__":
    rospy.init_node("ground_station_cli")
    ground_station = GroundStationCli()
    thread = threading.Thread(target=input_loop, args=(ground_station,))
    thread.daemon = True
    thread.start()

    try:
        rospy.spin()
    except KeyboardInterrupt:
        sys.exit(0)

