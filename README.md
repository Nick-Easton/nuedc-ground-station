# NUEDC ROS1 Ground-Air Starter

This starter workspace implements the first communication loop for a ROS 1 based ground station and onboard computer setup.

Recommended layout:

```text
catkin_ws/
  src/
    nuedc_ground_air/
```

Run `roscore` on the onboard computer. The ground station connects to the onboard computer as a remote ROS node.

## 1. Copy To Ubuntu ROS 1 Workspace

Place this repository as:

```bash
~/catkin_ws/src/nuedc_ground_air
```

Then build:

```bash
cd ~/catkin_ws
catkin_make
source devel/setup.bash
```

## 2. Network Setup

On onboard computer:

```bash
export ROS_MASTER_URI=http://172.20.10.11:11311
export ROS_IP=172.20.10.11
roscore
```

On ground station:

```bash
export ROS_MASTER_URI=http://172.20.10.11:11311
export ROS_IP=172.20.10.9
```

Replace IP addresses with your actual addresses.

## 3. First Demo

On onboard computer:

```bash
roslaunch nuedc_ground_air onboard_demo.launch
```

On ground station:

```bash
roslaunch nuedc_ground_air ground_cli.launch
```

Type commands in the ground station terminal:

```text
start
stop
abort
quit
```

Expected result:

- Ground station publishes `/mission/command`.
- Onboard mission node receives the command.
- Onboard node publishes `/mission/state`.
- Fake YOLO node publishes `/vision/detections`.
- Ground station prints mission state and fake detections.

## 4. Next Milestones

1. Replace `fake_yolo_node.py` with a real camera + YOLO node.
2. Add target position estimation from detection center, camera parameters, altitude, and yaw.
3. Add `fc_bridge_node` to read flight controller feedback.
4. Add low-speed setpoint output only after safety logic is tested.
5. Replace CLI ground station with a PyQt/rqt interface.

## 5. Use The NJUPT Qt LandScreen With ROS 1

The NJUPT ground station is not tied to ROS 2. It talks to the onboard computer through TCP JSON, so a ROS 1 system only needs a bridge node.

On the ROS 1 onboard computer:

```bash
cd ~/catkin_ws
catkin_make
source devel/setup.bash
chmod +x src/nuedc_ground_air/scripts/*.py
roslaunch nuedc_ground_air onboard_landscreen_demo.launch
```

On the Qt ground station computer:

```bash
cd ~/LandScreen-master/build
LANDSCREEN_SERVER_IP=<onboard_ros1_ip> LANDSCREEN_SERVER_PORT=8001 ./planescreen
```

The bridge receives forbidden-zone JSON from LandScreen, publishes `/mission/forbidden_zones`, sends `START` on `/mission/command` when `launch=true`, and relays ROS paths and detections back to LandScreen.

## 6. Ground-Planned Path Demo

Run the onboard nodes on `172.20.10.11`:

```bash
roslaunch nuedc_ground_air onboard_landscreen_demo.launch
```

Run the planner on the ground computer `172.20.10.9`:

```bash
roslaunch nuedc_ground_air ground_path_planner.launch
```

The data flow is:

```text
LandScreen -> /mission/forbidden_zones -> ground_path_planner
ground_path_planner -> /planner/path -> onboard_path_receiver
/planner/path -> LandScreen bridge -> Qt map
```

The onboard receiver publishes validation results on `/planner/path_ack`.
