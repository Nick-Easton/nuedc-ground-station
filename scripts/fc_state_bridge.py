#!/usr/bin/env python3
import math

import rospy
from geometry_msgs.msg import PoseStamped
from mavros_msgs.msg import State
from sensor_msgs.msg import BatteryState

from nuedc_ground_air.msg import DroneState


class FcStateBridge:
    def __init__(self):
        self.state_timeout = float(rospy.get_param("~state_timeout", 3.0))
        publish_rate = float(rospy.get_param("~publish_rate", 5.0))

        self.mode = "UNKNOWN"
        self.armed = False
        self.fcu_connected = False
        self.battery_percent = -1.0
        self.altitude_m = 0.0
        self.yaw_deg = 0.0
        self.last_state_time = None

        self.state_pub = rospy.Publisher("/drone/state", DroneState, queue_size=10)
        rospy.Subscriber("/mavros/state", State, self.on_mavros_state, queue_size=10)
        rospy.Subscriber("/mavros/battery", BatteryState, self.on_battery, queue_size=10)
        rospy.Subscriber(
            "/mavros/local_position/pose", PoseStamped, self.on_local_pose, queue_size=10
        )

        self.timer = rospy.Timer(rospy.Duration(1.0 / max(publish_rate, 0.1)), self.publish)
        rospy.loginfo("FC state bridge ready (telemetry only; no control commands)")

    def on_mavros_state(self, msg):
        self.mode = msg.mode or "UNKNOWN"
        self.armed = msg.armed
        self.fcu_connected = msg.connected
        self.last_state_time = rospy.Time.now()

    def on_battery(self, msg):
        if math.isfinite(msg.percentage) and msg.percentage >= 0.0:
            self.battery_percent = max(0.0, min(100.0, msg.percentage * 100.0))

    def on_local_pose(self, msg):
        self.altitude_m = msg.pose.position.z
        orientation = msg.pose.orientation
        sin_yaw = 2.0 * (
            orientation.w * orientation.z + orientation.x * orientation.y
        )
        cos_yaw = 1.0 - 2.0 * (
            orientation.y * orientation.y + orientation.z * orientation.z
        )
        self.yaw_deg = math.degrees(math.atan2(sin_yaw, cos_yaw))

    def publish(self, _event):
        now = rospy.Time.now()
        state_is_fresh = (
            self.last_state_time is not None
            and (now - self.last_state_time).to_sec() <= self.state_timeout
        )

        msg = DroneState()
        msg.header.stamp = now
        msg.mode = self.mode
        msg.battery_percent = self.battery_percent
        msg.altitude_m = self.altitude_m
        msg.yaw_deg = self.yaw_deg
        msg.armed = self.armed
        msg.connected = self.fcu_connected and state_is_fresh
        self.state_pub.publish(msg)


if __name__ == "__main__":
    rospy.init_node("fc_state_bridge")
    FcStateBridge()
    rospy.spin()
