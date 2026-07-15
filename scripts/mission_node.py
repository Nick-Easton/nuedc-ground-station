#!/usr/bin/env python3
import rospy

from nuedc_ground_air.msg import Detection2D, ForbiddenZones, MissionCommand, MissionState


class MissionNode:
    def __init__(self):
        self.state = "IDLE"
        self.detail = "waiting for ground command"
        self.command_seq = 0
        self.last_detection_time = None

        self.state_pub = rospy.Publisher("/mission/state", MissionState, queue_size=10)
        rospy.Subscriber("/mission/command", MissionCommand, self.on_command)
        rospy.Subscriber("/mission/forbidden_zones", ForbiddenZones, self.on_forbidden_zones)
        rospy.Subscriber("/vision/detections", Detection2D, self.on_detection)

        self.timer = rospy.Timer(rospy.Duration(0.2), self.publish_state)

    def on_command(self, msg):
        command = msg.command.strip().upper()
        self.command_seq += 1

        if command == "START":
            self.state = "SEARCH"
            self.detail = "mission started, searching target"
        elif command == "STOP":
            self.state = "IDLE"
            self.detail = "mission stopped by ground station"
        elif command == "ABORT":
            self.state = "ABORT"
            self.detail = "emergency abort from ground station"
        else:
            self.detail = "unknown command: {}".format(msg.command)

        rospy.loginfo("Mission command received: %s -> %s", command, self.state)

    def on_forbidden_zones(self, msg):
        zones = [(msg.f1x, msg.f1y), (msg.f2x, msg.f2y), (msg.f3x, msg.f3y)]
        zones = [zone for zone in zones if zone != (-1, -1)]
        if zones:
            self.detail = "forbidden zones updated: {}".format(zones)

    def on_detection(self, msg):
        if self.state != "SEARCH":
            return

        if msg.confidence >= 0.60:
            self.state = "DETECT"
            self.detail = "target {} detected, confidence {:.2f}".format(
                msg.class_name, msg.confidence
            )
            self.last_detection_time = rospy.Time.now()

    def publish_state(self, _event):
        if self.state == "DETECT" and self.last_detection_time is not None:
            lost_for = (rospy.Time.now() - self.last_detection_time).to_sec()
            if lost_for > 1.0:
                self.state = "SEARCH"
                self.detail = "target lost, resume searching"

        msg = MissionState()
        msg.header.stamp = rospy.Time.now()
        msg.state = self.state
        msg.detail = self.detail
        msg.command_seq = self.command_seq
        self.state_pub.publish(msg)


if __name__ == "__main__":
    rospy.init_node("mission_node")
    MissionNode()
    rospy.spin()
