#!/usr/bin/env python3
"""Normalize lidar odometry and mission state into the read-only monitor protocol."""

import json
import math

import rospy
from nav_msgs.msg import Odometry
from std_msgs.msg import String

from nuedc_ground_air.msg import DroneState, MissionState


def yaw_degrees(orientation):
    sin_yaw = 2.0 * (
        orientation.w * orientation.z + orientation.x * orientation.y
    )
    cos_yaw = 1.0 - 2.0 * (
        orientation.y * orientation.y + orientation.z * orientation.z
    )
    return math.degrees(math.atan2(sin_yaw, cos_yaw))


class GroundAirTelemetryAdapter:
    """Read-only ROS adapter. It never publishes vehicle control topics."""

    def __init__(self):
        self.source = rospy.get_param("~source", "lidar")
        self.stale_timeout = float(rospy.get_param("~stale_timeout", 0.6))
        self.mission_mode = rospy.get_param("~mission_mode", "DROP")
        self.mission_id = rospy.get_param("~mission_id", "--")
        publish_rate = max(1.0, float(rospy.get_param("~publish_rate", 10.0)))

        self.car_odom = None
        self.drone_odom = None
        self.car_received = None
        self.drone_received = None
        self.drone_state = None
        self.mission_state = "IDLE"
        self.mission_started = None

        output_topic = rospy.get_param(
            "~output_topic", "/ground_station/telemetry"
        )
        car_topic = rospy.get_param("~car_odometry_topic", "/lidar/car/odom")
        drone_topic = rospy.get_param(
            "~drone_odometry_topic", "/lidar/drone/odom"
        )
        drone_state_topic = rospy.get_param("~drone_state_topic", "/drone/state")
        mission_state_topic = rospy.get_param(
            "~mission_state_topic", "/mission/state"
        )

        self.publisher = rospy.Publisher(output_topic, String, queue_size=10)
        rospy.Subscriber(car_topic, Odometry, self.on_car_odom, queue_size=10)
        rospy.Subscriber(drone_topic, Odometry, self.on_drone_odom, queue_size=10)
        rospy.Subscriber(
            drone_state_topic, DroneState, self.on_drone_state, queue_size=10
        )
        rospy.Subscriber(
            mission_state_topic, MissionState, self.on_mission_state, queue_size=10
        )
        self.timer = rospy.Timer(rospy.Duration(1.0 / publish_rate), self.publish)
        rospy.loginfo(
            "Ground-air monitor adapter ready: car=%s drone=%s output=%s",
            car_topic,
            drone_topic,
            output_topic,
        )

    def on_car_odom(self, msg):
        self.car_odom = msg
        self.car_received = rospy.Time.now()

    def on_drone_odom(self, msg):
        self.drone_odom = msg
        self.drone_received = rospy.Time.now()

    def on_drone_state(self, msg):
        self.drone_state = msg

    def on_mission_state(self, msg):
        state = (msg.state or "IDLE").strip().upper()
        if self.mission_state in ("IDLE", "COMPLETE", "FAILED", "ABORT") and state not in (
            "IDLE",
            "COMPLETE",
            "FAILED",
            "ABORT",
        ):
            self.mission_started = rospy.Time.now()
        self.mission_state = state

    def is_fresh(self, received, now):
        return received is not None and (now - received).to_sec() <= self.stale_timeout

    @staticmethod
    def pose_payload(odom, valid, battery_percent=-1.0, altitude_override=None,
                     yaw_override=None):
        if odom is None:
            return {
                "valid": False,
                "x_m": 0.0,
                "y_m": 0.0,
                "z_m": 0.0,
                "yaw_deg": 0.0,
                "speed_mps": 0.0,
                "battery_percent": battery_percent,
            }
        position = odom.pose.pose.position
        linear = odom.twist.twist.linear
        altitude = position.z if altitude_override is None else altitude_override
        yaw = yaw_degrees(odom.pose.pose.orientation) if yaw_override is None else yaw_override
        return {
            "valid": valid,
            "x_m": round(position.x, 4),
            "y_m": round(position.y, 4),
            "z_m": round(altitude, 4),
            "yaw_deg": round(yaw, 2),
            "speed_mps": round(math.hypot(linear.x, linear.y), 4),
            "battery_percent": round(battery_percent, 1),
        }

    def publish(self, _event):
        now = rospy.Time.now()
        car_fresh = self.is_fresh(self.car_received, now)
        drone_fresh = self.is_fresh(self.drone_received, now)
        drone_state = self.drone_state
        battery = drone_state.battery_percent if drone_state is not None else -1.0
        connected = bool(drone_state.connected) if drone_state is not None else drone_fresh
        elapsed = (
            max(0.0, (now - self.mission_started).to_sec())
            if self.mission_started is not None
            else 0.0
        )

        ages = []
        if self.car_received is not None:
            ages.append((now - self.car_received).to_sec())
        if self.drone_received is not None:
            ages.append((now - self.drone_received).to_sec())
        latency_ms = int(max(ages) * 1000.0) if ages else -1

        payload = {
            "type": "ground_air_telemetry",
            "timestamp_ms": int(now.to_sec() * 1000.0),
            "source": self.source,
            "latency_ms": latency_ms,
            "car": self.pose_payload(self.car_odom, car_fresh),
            "drone": self.pose_payload(
                self.drone_odom,
                drone_fresh,
                battery_percent=battery,
            ),
            "mission": {
                "mission_id": self.mission_id,
                "mode": self.mission_mode,
                "state": self.mission_state,
                "elapsed_s": round(elapsed, 2),
                "drop_done": self.mission_state in (
                    "DROP",
                    "RETURN_HOME",
                    "LANDING",
                    "COMPLETE",
                ),
                "touchdown_confirmed": self.mission_state in (
                    "TOUCHDOWN",
                    "HOLD_ON_CAR",
                ),
            },
            "links": {
                "localization_ok": car_fresh and drone_fresh,
                "car_link_ok": car_fresh,
                "drone_link_ok": drone_fresh and connected,
            },
        }
        payload["drone"].update(
            {
                "armed": bool(drone_state.armed) if drone_state is not None else False,
                "flight_mode": drone_state.mode if drone_state is not None else "--",
                "target_visible": False,
                "target_confidence": 0.0,
            }
        )
        self.publisher.publish(
            String(data=json.dumps(payload, separators=(",", ":"), ensure_ascii=False))
        )


if __name__ == "__main__":
    rospy.init_node("ground_air_telemetry_adapter")
    GroundAirTelemetryAdapter()
    rospy.spin()
