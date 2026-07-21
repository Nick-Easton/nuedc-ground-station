# ROS 1 TensorRT YOLO Deployment

This document describes the tested onboard recognition flow for a Jetson Orin NX, a USB camera, Ubuntu 20.04, and ROS 1 Noetic.

## Architecture

```text
Qt LandScreen
  -> TCP JSON on port 8001
  -> /mission/command (START or STOP)
  -> USB camera + TensorRT YOLO
  -> /yolo_trt_node/detections
  -> /vision/detections and /vision/summary
  -> TCP bridge
  -> Qt target-information view
```

The ground station does not need to join the onboard ROS master. It communicates with `landscreen_ros1_bridge.py` over TCP.

## Workspace Layout

The integration expects two catkin workspaces on the onboard computer:

```text
~/catkin_ws/src/nuedc_ground_air
~/catkin_ws_yolo_trt/src/yolo_trt_ros
```

The TensorRT engine defaults to:

```text
~/catkin_ws_yolo_trt/src/yolo_trt_ros/models/best.engine
```

Build the integration workspace:

```bash
cd ~/catkin_ws
catkin_make
chmod +x src/nuedc_ground_air/scripts/*.py
```

## Start the Idle Services

```bash
source /opt/ros/noetic/setup.bash
source ~/catkin_ws/devel/setup.bash
roslaunch nuedc_ground_air landscreen_ros1_bridge.launch \
  camera:=/dev/video0 show_window:=false
```

The idle launch starts path planning, path reception, the TCP bridge, the grid state machine, and the START/STOP listener. The grid state machine must already be subscribed when START is published. It does not open the camera or run YOLO while idle.

When LandScreen sends `launch=true`, the listener starts:

- `/usb_cam`
- `/yolo_trt_node`
- `/vision_detection_adapter`

When LandScreen sends `launch=false`, or the mission state becomes terminal, the camera, YOLO, detection adapter, and any OpenCV preview stop. The grid state machine remains idle and waits for the next START. The preview is disabled by default so the same launch works from the headless user service; set `show_window:=true` only for desktop debugging.

The managed child process sources the workspaces in this order: ROS Noetic, `catkin_ws_yolo_trt`, then `catkin_ws`. Keeping the application workspace last preserves its generated messages and package paths.

## Main Topics

```text
/usb_cam/image_raw                 sensor_msgs/Image
/yolo_trt_node/detections         vision_msgs/Detection2DArray
/yolo_trt_node/annotated          sensor_msgs/Image
/vision/detections                nuedc_ground_air/Detection2D
/vision/summary                   std_msgs/String (JSON)
/vision/grid_result               std_msgs/String (JSON)
/mission/vision_goal              geometry_msgs/PoseStamped, frame_id=base_link (disabled by default)
/current_grid                     std_msgs/String, for example A3B4
/mission/global_path              nav_msgs/Path, frame_id=mission
/planner/path_ack                 std_msgs/String (JSON)
```

`mission` is fixed when the mission starts: its origin is the A9B1 takeoff point,
x points forward along the takeoff heading, y points left, and z points up. The
default `global_path_use_pose_z=false` publishes zero for every path pose z;
the flight executor should use `takeoff_height=1.2` m instead. When enabled,
the planner writes that height into each pose z.

Example summary:

```json
{"total":3,"counts":{"monkey":1,"tiger":2}}
```

## Grid Association

Until flight-controller localization is connected, live targets appear as position pending. A flight-controller bridge can publish the current grid:

```bash
rostopic pub -1 /current_grid std_msgs/String "data: 'A3B4'"
```

The grid state machine waits for stable hover, samples detections, publishes one result per grid, and prevents duplicate records for the same grid during one mission.

The vision adapter does not advertise or publish `/mission/vision_goal` unless
`publish_vision_goal:=true` is explicitly set. When enabled, the same class must
be observed near the same image position in at least 3 of the latest 5 processed
frames. The default confirmation radius is 40 pixels, publishing cooldown is
2 seconds, and a same-class target within 60 pixels is emitted only once during
the vision process. The goal contains a bounded image-derived translation in
`base_link`; it keeps z at zero and uses the identity quaternion. It is not a
calibrated metric position and must not drive real flight before camera
calibration, distance estimation, ground projection, and frame transformation
are complete. Grid counts likewise require support from at least three frames.

## Verification

```bash
rostopic echo /vision/summary
rostopic echo /vision/grid_result
rostopic echo /planner/path_ack
```

To view the annotated topic without the OpenCV window:

```bash
rqt_image_view /yolo_trt_node/annotated
```
