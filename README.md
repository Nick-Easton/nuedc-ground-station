# NUEDC ROS 1 空地通信起步工程

本工作空间实现了基于 ROS 1 的地面站与机载电脑之间的基础通信闭环。

推荐目录结构：

```text
catkin_ws/
  src/
    nuedc_ground_air/
```

在机载电脑上运行 `roscore`，地面站作为远程 ROS 节点连接到机载电脑。

## 1. 复制到 Ubuntu ROS 1 工作空间

将本仓库放置到以下位置：

```bash
~/catkin_ws/src/nuedc_ground_air
```

然后进行编译：

```bash
cd ~/catkin_ws
catkin_make
source devel/setup.bash
```

## 2. 网络配置

在机载电脑上执行：

```bash
export ROS_MASTER_URI=http://172.20.10.11:11311
export ROS_IP=172.20.10.11
roscore
```

在地面站电脑上执行：

```bash
export ROS_MASTER_URI=http://172.20.10.11:11311
export ROS_IP=172.20.10.9
```

请根据实际网络环境替换上述 IP 地址。

## 3. 基础通信演示

在机载电脑上执行：

```bash
roslaunch nuedc_ground_air onboard_demo.launch
```

在地面站电脑上执行：

```bash
roslaunch nuedc_ground_air ground_cli.launch
```

在地面站终端中输入以下命令：

```text
start
stop
abort
quit
```

预期结果：

- 地面站向 `/mission/command` 发布任务命令。
- 机载任务节点接收该命令。
- 机载节点向 `/mission/state` 发布任务状态。
- 模拟 YOLO 节点向 `/vision/detections` 发布检测结果。
- 地面站打印任务状态和模拟检测结果。

## 4. 后续开发目标

1. 使用真实相机和 YOLO 节点替换 `fake_yolo_node.py`。
2. 根据检测框中心、相机参数、飞行高度和偏航角估算目标位置。
3. 在安全逻辑经过充分测试后，再增加低速控制设定值输出。
4. 继续完善现有 PyQt 地面站界面，或增加 rqt 界面。

## 5. 在 ROS 1 中使用 NJUPT Qt LandScreen

NJUPT 地面站并不依赖 ROS 2。它通过 TCP JSON 与机载电脑通信，因此 ROS 1 系统只需要增加一个桥接节点。

在 ROS 1 机载电脑上执行：

```bash
cd ~/catkin_ws
catkin_make
source devel/setup.bash
chmod +x src/nuedc_ground_air/scripts/*.py
roslaunch nuedc_ground_air onboard_landscreen_demo.launch
```

在 Qt 地面站电脑上执行：

```bash
cd ~/LandScreen-master/build
LANDSCREEN_SERVER_IP=<onboard_ros1_ip> LANDSCREEN_SERVER_PORT=8001 ./planescreen
```

桥接节点接收 LandScreen 发来的禁区 JSON，并发布到 `/mission/forbidden_zones`。当 `launch=true` 时，它会向 `/mission/command` 发送 `START`，同时将 ROS 路径和目标检测结果回传给 LandScreen。

## 6. 地面端路径规划演示

在 IP 为 `172.20.10.11` 的机载电脑上启动节点：

```bash
roslaunch nuedc_ground_air onboard_landscreen_demo.launch
```

在 IP 为 `172.20.10.9` 的地面站电脑上启动规划节点：

```bash
roslaunch nuedc_ground_air ground_path_planner.launch
```

数据流如下：

```text
LandScreen -> /mission/forbidden_zones -> ground_path_planner
ground_path_planner -> /planner/path -> onboard_path_receiver
/planner/path -> LandScreen bridge -> Qt map
```

机载接收节点会通过 `/planner/path_ack` 发布路径校验结果。

规划器先按行生成往复式覆盖顺序，再用四邻域最短路连接相邻覆盖目标。禁飞格不会出现在航点中，任意连续航点也只会移动到上下左右相邻的自由格，因此不会再用长直线跨过被跳过的禁飞格。为绕开禁飞格，路径可能重复经过部分自由格，最终航点数可能大于自由格数量。桥回传路径时会附带 `forbidden:[{"a":A,"b":B}]`，LandScreen 据此更新标签，并用半透明红色方块和叉号标出禁飞格；旧客户端可以忽略该新增字段。

## 7. PX4 飞控遥测接入

第一阶段只读取飞控状态，不调用解锁、模式切换、起飞或位置控制服务。通过 USB 连接 Pixhawk 时，先确认串口设备：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

使用常见的 Pixhawk USB 设备启动 MAVROS 和状态桥：

```bash
roslaunch nuedc_ground_air onboard_fc_telemetry.launch \
  fcu_url:=/dev/ttyACM0:57600
```

如果使用 USB 转串口模块，设备通常是 `/dev/ttyUSB0`；如果使用 Jetson UART，则应传入实际串口和飞控端配置一致的波特率，例如：

```bash
roslaunch nuedc_ground_air onboard_fc_telemetry.launch \
  fcu_url:=/dev/ttyTHS1:921600
```

检查 MAVROS 与统一状态 topic：

```bash
rostopic echo /mavros/state
rostopic echo /drone/state
```

`/mavros/state.connected` 为 `True` 表示 MAVLink 心跳已经建立。`fc_state_bridge.py` 将模式、解锁状态、电池百分比、本地高度和偏航角整理为 `/drone/state`；超过 `3 s` 没有状态更新时会将 `connected` 置为 `False`。

如果 MAVROS 已由其他启动文件运行，仅启动状态桥，避免重复占用串口：

```bash
roslaunch nuedc_ground_air onboard_fc_telemetry.launch start_mavros:=false
```

如果 MAVROS 可以打开 `/dev/ttyUSB0`，但 `/mavros/state.connected` 始终为 `False`，先确认串口是否真的收到飞控数据，再继续调整 ROS：

```bash
stty -F /dev/ttyUSB0 raw 921600 cs8 -cstopb -parenb
timeout 2 dd if=/dev/ttyUSB0 bs=64 count=1 status=none | wc -c
```

结果持续为 `0` 表示飞控没有向该串口发送数据。PX4 配套计算机通常连接 `TELEM2`，接线应为飞控 `TX -> CP2102 RX`、飞控 `RX -> CP2102 TX`、`GND -> GND`。飞控单独供电时不要连接适配器的 VCC，避免反向供电。PX4 侧应检查 `MAV_1_CONFIG=TELEM2`、`MAV_1_MODE=Onboard`，并让 `SER_TEL2_BAUD` 与 MAVROS 的 `fcu_url` 波特率一致；修改后重启飞控。

## 8. 真实 YOLO 检测接入

机载真实识别包位于 `~/catkin_ws_yolo_trt`。`yolo_trt_ros` 读取 USB 摄像头并发布标准消息：

```text
/usb_cam/image_raw              sensor_msgs/Image
/yolo_trt_node/detections       vision_msgs/Detection2DArray
/yolo_trt_node/annotated        sensor_msgs/Image
```

`vision_detection_adapter.py` 将它转换成空地通信统一使用的：

```text
/vision/detections  nuedc_ground_air/Detection2D
```

完整数据流为：

```text
USB Camera -> /usb_cam/image_raw -> yolo_trt_ros
yolo_trt_ros -> /yolo_trt_node/detections -> vision_detection_adapter
vision_detection_adapter -> /vision/detections -> landscreen_ros1_bridge
landscreen_ros1_bridge -> TCP JSON -> LandScreen 目标信息
```

启动前必须按顺序加载两个工作区；后加载的 `nuedc_ground_air` 工作区会叠加在 YOLO 工作区之上：

```bash
source /opt/ros/noetic/setup.bash
source ~/catkin_ws_yolo_trt/devel/setup.bash
source ~/catkin_ws/devel/setup.bash
roslaunch nuedc_ground_air onboard_real_vision_demo.launch \
  camera:=/dev/video0 show_window:=false
```

如果 YOLO 与摄像头已经由其他终端启动，只启动空地通信与检测适配器：

```bash
roslaunch nuedc_ground_air onboard_real_vision_demo.launch start_yolo:=false
```

查看带框画面：

```bash
rqt_image_view /yolo_trt_node/annotated
```

适配器将类别编号映射为 `elephant`、`tiger`、`monkey`、`kongque`、`wolf`，并额外发布 JSON 统计 `/vision/summary`。该启动文件会关闭 `fake_yolo_node.py`，避免模拟检测与真实检测同时发布。默认置信度为 `0.60`，默认最多以 `5 Hz` 转发每帧置信度最高的 10 个目标；这些值均可通过 launch 参数修改。

LandScreen 的绿色“发送”按钮只发送禁飞区和任务信息。识别结果由桥接节点自动转发到“显示目标信息”页面。当前只回传类别、置信度和检测框；桥内由像素中心换算场地坐标的逻辑仍是模拟占位，不代表真实目标定位。

2026-07-15 已在机载电脑使用 `/dev/video0` 验证 USB Camera 和 TensorRT YOLO：`/usb_cam/image_raw` 与 `/yolo_trt_node/annotated` 均稳定在约 `30 Hz`，空场景会发布空检测数组和 `{"counts":{},"total":0}`。真实动物正样本识别仍需摆放对应图片或实物验证。
