#!/usr/bin/env python3
import json
import socket
import threading

import rospy
from nav_msgs.msg import Path

from nuedc_ground_air.msg import Detection2D, ForbiddenZones, MissionCommand, MissionState


class LandScreenRos1Bridge:
    def __init__(self):
        self.host = rospy.get_param("~host", "0.0.0.0")
        self.port = int(rospy.get_param("~port", 8001))
        self.clients = []
        self.clients_lock = threading.Lock()
        self.last_launch = False

        self.zones_pub = rospy.Publisher("/mission/forbidden_zones", ForbiddenZones, queue_size=10)
        self.command_pub = rospy.Publisher("/mission/command", MissionCommand, queue_size=10)
        rospy.Subscriber("/mission/state", MissionState, self.on_mission_state)
        rospy.Subscriber("/vision/detections", Detection2D, self.on_detection)
        rospy.Subscriber("/planner/path", Path, self.on_path)

    def serve_forever(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(4)
        rospy.loginfo("LandScreen ROS1 bridge listening on %s:%d", self.host, self.port)

        while not rospy.is_shutdown():
            try:
                client, address = server.accept()
            except OSError:
                break

            rospy.loginfo("LandScreen client connected: %s:%d", address[0], address[1])
            with self.clients_lock:
                self.clients.append(client)
            thread = threading.Thread(target=self.handle_client, args=(client, address))
            thread.daemon = True
            thread.start()

    def handle_client(self, client, address):
        buffer = b""
        try:
            while not rospy.is_shutdown():
                data = client.recv(4096)
                if not data:
                    break
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line = line.strip()
                    if line:
                        self.handle_ground_json(line)
        except OSError:
            pass
        finally:
            rospy.loginfo("LandScreen client disconnected: %s:%d", address[0], address[1])
            with self.clients_lock:
                if client in self.clients:
                    self.clients.remove(client)
            try:
                client.close()
            except OSError:
                pass

    def handle_ground_json(self, line):
        try:
            data = json.loads(line.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            rospy.logwarn("Bad LandScreen JSON: %s", exc)
            return

        zones = ForbiddenZones()
        zones.header.stamp = rospy.Time.now()
        zones.f1x = int(data.get("f1x", -1))
        zones.f1y = int(data.get("f1y", -1))
        zones.f2x = int(data.get("f2x", -1))
        zones.f2y = int(data.get("f2y", -1))
        zones.f3x = int(data.get("f3x", -1))
        zones.f3y = int(data.get("f3y", -1))
        zones.launch = bool(data.get("launch", False))
        self.zones_pub.publish(zones)

        if zones.launch and not self.last_launch:
            self.publish_command("START")
        self.last_launch = zones.launch

        rospy.loginfo(
            "LandScreen zones: f1=(%d,%d) f2=(%d,%d) f3=(%d,%d) launch=%s",
            zones.f1x,
            zones.f1y,
            zones.f2x,
            zones.f2y,
            zones.f3x,
            zones.f3y,
            zones.launch,
        )

    def publish_command(self, command):
        msg = MissionCommand()
        msg.header.stamp = rospy.Time.now()
        msg.command = command
        self.command_pub.publish(msg)

    def on_mission_state(self, msg):
        if msg.state == "IDLE":
            return
        self.send_to_ground({"planner": [], "tx": -1, "ty": -1, "tn": "NULL"})

    def on_detection(self, msg):
        if msg.confidence < 0.60:
            return

        # Until real localization is connected, map image-center detections into a small demo field.
        x = max(0.0, min(4.5, msg.center_x / 100.0))
        y = max(0.0, min(3.5, msg.center_y / 100.0))
        self.send_to_ground(
            {
                "planner": [],
                "tx": x,
                "ty": y,
                "tn": msg.class_name or "target",
            }
        )

    def on_path(self, msg):
        planner = []
        for pose in msg.poses:
            planner.append(
                {
                    "x": round(pose.pose.position.x, 3),
                    "y": round(pose.pose.position.y, 3),
                }
            )
        self.send_to_ground({"planner": planner, "tx": -1, "ty": -1, "tn": "NULL"})
        rospy.loginfo("Forwarded ground path to LandScreen: %d points", len(planner))

    def send_to_ground(self, payload):
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        with self.clients_lock:
            clients = list(self.clients)

        for client in clients:
            try:
                client.sendall(data)
            except OSError:
                with self.clients_lock:
                    if client in self.clients:
                        self.clients.remove(client)

if __name__ == "__main__":
    rospy.init_node("landscreen_ros1_bridge")
    bridge = LandScreenRos1Bridge()
    bridge.serve_forever()
