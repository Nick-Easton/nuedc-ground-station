#!/usr/bin/env bash
set -eo pipefail

source /opt/ros/noetic/setup.bash
source "${HOME}/catkin_ws/devel/setup.bash"

ground_ip="${GROUND_ROS_IP:-$(hostname -I | awk '{print $1}')}"
export ROS_MASTER_URI="${ROS_MASTER_URI:-http://${ground_ip}:11311}"
export ROS_IP="${ROS_IP:-${ground_ip}}"

exec roslaunch nuedc_ground_air landscreen_ground_planner.launch "$@"
