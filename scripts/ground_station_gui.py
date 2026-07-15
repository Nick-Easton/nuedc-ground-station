#!/usr/bin/env python3
import sys

import rospy
from PyQt5 import QtCore, QtWidgets

from nuedc_ground_air.msg import Detection2D, DroneState, MissionCommand, MissionState


class RosBridge(QtCore.QObject):
    mission_state_changed = QtCore.pyqtSignal(str, str, int)
    detection_changed = QtCore.pyqtSignal(str, float, float, float, float, float)
    drone_state_changed = QtCore.pyqtSignal(str, float, float, float, bool, bool)
    log_added = QtCore.pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.command_pub = rospy.Publisher("/mission/command", MissionCommand, queue_size=10)
        rospy.Subscriber("/mission/state", MissionState, self.on_mission_state)
        rospy.Subscriber("/vision/detections", Detection2D, self.on_detection)
        rospy.Subscriber("/drone/state", DroneState, self.on_drone_state)

    def publish_command(self, command):
        msg = MissionCommand()
        msg.header.stamp = rospy.Time.now()
        msg.command = command.upper()
        self.command_pub.publish(msg)
        self.log_added.emit("Command sent: {}".format(msg.command))

    def on_mission_state(self, msg):
        self.mission_state_changed.emit(msg.state, msg.detail, msg.command_seq)

    def on_detection(self, msg):
        self.detection_changed.emit(
            msg.class_name,
            msg.confidence,
            msg.center_x,
            msg.center_y,
            msg.width,
            msg.height,
        )

    def on_drone_state(self, msg):
        self.drone_state_changed.emit(
            msg.mode,
            msg.battery_percent,
            msg.altitude_m,
            msg.yaw_deg,
            msg.armed,
            msg.connected,
        )


class GroundStationWindow(QtWidgets.QMainWindow):
    def __init__(self, bridge):
        super().__init__()
        self.bridge = bridge
        self.setWindowTitle("NUEDC Wildlife Patrol Ground Station")
        self.resize(920, 560)

        self.state_value = QtWidgets.QLabel("IDLE")
        self.detail_value = QtWidgets.QLabel("waiting")
        self.seq_value = QtWidgets.QLabel("0")
        self.detection_value = QtWidgets.QLabel("no target")
        self.drone_value = QtWidgets.QLabel("not connected")
        self.log_view = QtWidgets.QPlainTextEdit()
        self.log_view.setReadOnly(True)

        self.start_button = QtWidgets.QPushButton("Start Patrol")
        self.stop_button = QtWidgets.QPushButton("Stop")
        self.abort_button = QtWidgets.QPushButton("Abort")
        self.abort_button.setObjectName("abortButton")

        self.start_button.clicked.connect(lambda: self.bridge.publish_command("START"))
        self.stop_button.clicked.connect(lambda: self.bridge.publish_command("STOP"))
        self.abort_button.clicked.connect(lambda: self.bridge.publish_command("ABORT"))

        self.bridge.mission_state_changed.connect(self.update_mission_state)
        self.bridge.detection_changed.connect(self.update_detection)
        self.bridge.drone_state_changed.connect(self.update_drone_state)
        self.bridge.log_added.connect(self.append_log)

        self.setCentralWidget(self.build_layout())
        self.apply_style()

        self.shutdown_timer = QtCore.QTimer(self)
        self.shutdown_timer.timeout.connect(self.close_if_ros_shutdown)
        self.shutdown_timer.start(200)

    def build_layout(self):
        root = QtWidgets.QWidget()
        main = QtWidgets.QVBoxLayout(root)
        main.setContentsMargins(18, 18, 18, 18)
        main.setSpacing(14)

        title = QtWidgets.QLabel("Wildlife Patrol Ground Station")
        title.setObjectName("title")
        main.addWidget(title)

        cards = QtWidgets.QGridLayout()
        cards.setSpacing(12)
        cards.addWidget(self.make_card("Mission State", self.state_value), 0, 0)
        cards.addWidget(self.make_card("Mission Detail", self.detail_value), 0, 1)
        cards.addWidget(self.make_card("Command Seq", self.seq_value), 0, 2)
        cards.addWidget(self.make_card("Detection", self.detection_value), 1, 0, 1, 2)
        cards.addWidget(self.make_card("Drone State", self.drone_value), 1, 2)
        main.addLayout(cards)

        controls = QtWidgets.QHBoxLayout()
        controls.setSpacing(10)
        controls.addWidget(self.start_button)
        controls.addWidget(self.stop_button)
        controls.addWidget(self.abort_button)
        controls.addStretch(1)
        main.addLayout(controls)

        log_label = QtWidgets.QLabel("Event Log")
        log_label.setObjectName("sectionLabel")
        main.addWidget(log_label)
        main.addWidget(self.log_view, 1)
        return root

    def make_card(self, name, value_widget):
        box = QtWidgets.QGroupBox(name)
        layout = QtWidgets.QVBoxLayout(box)
        value_widget.setWordWrap(True)
        value_widget.setObjectName("cardValue")
        layout.addWidget(value_widget)
        return box

    def apply_style(self):
        self.setStyleSheet(
            """
            QMainWindow { background: #f4f6f8; }
            QLabel#title {
                color: #17202a;
                font-size: 24px;
                font-weight: 700;
            }
            QLabel#sectionLabel {
                color: #273746;
                font-size: 15px;
                font-weight: 700;
            }
            QGroupBox {
                background: white;
                border: 1px solid #d7dee8;
                border-radius: 6px;
                color: #4d5b6a;
                font-weight: 600;
                padding: 14px 10px 10px 10px;
            }
            QLabel#cardValue {
                color: #17202a;
                font-size: 18px;
                font-weight: 600;
            }
            QPushButton {
                min-width: 130px;
                min-height: 38px;
                border: 0;
                border-radius: 6px;
                color: white;
                background: #1f7a8c;
                font-size: 14px;
                font-weight: 700;
            }
            QPushButton:hover { background: #16606f; }
            QPushButton#abortButton { background: #b23a48; }
            QPushButton#abortButton:hover { background: #8f2e39; }
            QPlainTextEdit {
                background: #111827;
                color: #d1d5db;
                border-radius: 6px;
                padding: 10px;
                font-family: Consolas, monospace;
                font-size: 13px;
            }
            """
        )

    def update_mission_state(self, state, detail, seq):
        self.state_value.setText(state)
        self.detail_value.setText(detail)
        self.seq_value.setText(str(seq))

    def update_detection(self, class_name, confidence, center_x, center_y, width, height):
        self.detection_value.setText(
            "{} | conf {:.2f} | center ({:.1f}, {:.1f}) | size {:.1f} x {:.1f}".format(
                class_name, confidence, center_x, center_y, width, height
            )
        )

    def update_drone_state(self, mode, battery, altitude, yaw, armed, connected):
        self.drone_value.setText(
            "{} | battery {:.0f}% | alt {:.1f}m | yaw {:.1f} | armed {} | link {}".format(
                mode,
                battery,
                altitude,
                yaw,
                "yes" if armed else "no",
                "ok" if connected else "lost",
            )
        )

    def append_log(self, text):
        stamp = QtCore.QDateTime.currentDateTime().toString("HH:mm:ss")
        self.log_view.appendPlainText("[{}] {}".format(stamp, text))

    def close_if_ros_shutdown(self):
        if rospy.is_shutdown():
            self.close()

    def closeEvent(self, event):
        if not rospy.is_shutdown():
            rospy.signal_shutdown("ground station gui closed")
        super().closeEvent(event)


def main():
    rospy.init_node("ground_station_gui", disable_signals=True)
    app = QtWidgets.QApplication(sys.argv)
    bridge = RosBridge()
    window = GroundStationWindow(bridge)
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
