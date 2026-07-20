cmake_minimum_required(VERSION 3.0.2)
project(nuedc_ground_air)

find_package(catkin REQUIRED COMPONENTS
  geometry_msgs
  message_generation
  nav_msgs
  rospy
  std_msgs
)

add_message_files(
  FILES
  Detection2D.msg
  DroneState.msg
  ForbiddenZones.msg
  MissionCommand.msg
  MissionState.msg
)

generate_messages(
  DEPENDENCIES
  geometry_msgs
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS geometry_msgs message_runtime nav_msgs rospy std_msgs
)

catkin_install_python(PROGRAMS
  scripts/fc_state_bridge.py
  scripts/fake_yolo_node.py
  scripts/ground_path_planner.py
  scripts/grid_recognition_state_machine.py
  scripts/ground_station_gui.py
  scripts/landscreen_ros1_bridge.py
  scripts/ground_station_cli.py
  scripts/mission_node.py
  scripts/onboard_path_receiver.py
  scripts/vision_detection_adapter.py
  scripts/vision_start_on_command.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(PROGRAMS
  scripts/start_ground_global_path.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)
