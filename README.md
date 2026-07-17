# NUEDC ROS 1 空地通信起步工程

本工作空间实现了基于 ROS 1 的地面站与机载电脑之间的基础通信闭环。

推荐目录结构：

```text
catkin_ws/
  src/
    nuedc_ground_air/
```

比赛部署中，ROS Master、路径规划、视觉适配和 TCP 桥均运行在机载 NX；地面站 Nano 只运行 Qt UI，通过 TCP 8001 连接 NX，不需要加入 ROS Master。

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

在机载电脑上执行（按现场地址替换 `<nx_ip>`）：

```bash
export ROS_MASTER_URI=http://<nx_ip>:11311
export ROS_IP=<nx_ip>
roscore
```

地面站 Nano 只需确认 NX 的 TCP 端口可达：

```bash
ping <nx_ip>
timeout 3 bash -c 'echo >/dev/tcp/<nx_ip>/8001'
```

仅连接同一 Wi-Fi 不一定代表设备互通；热点或路由器的客户端隔离会导致 UI 一直停在“航线规划中”。

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

比赛集成模式在 ROS 1 机载电脑上执行：

```bash
cd ~/catkin_ws
catkin_make
source /opt/ros/noetic/setup.bash
source ~/catkin_ws_yolo_trt/devel/setup.bash
source devel/setup.bash
chmod +x src/nuedc_ground_air/scripts/*.py
roslaunch nuedc_ground_air landscreen_ros1_bridge.launch camera:=/dev/video0
```

需要在 NX 用户登录后自动启动并在异常退出时自动重启时，安装用户级服务：

```bash
cd ~/catkin_ws/src/nuedc_ground_air
bash tools/install_onboard_user_service.sh
```

检查和重启服务：

```bash
systemctl --user status nuedc-ground-stack.service
systemctl --user restart nuedc-ground-stack.service
```

在 Qt 地面站电脑上执行：

```bash
cd ~/LandScreen-master/build
./planescreen
```

首次启动时在 UI 右下角“连接设置”填写 NX 的 IP 和端口 8001；配置由 `QSettings` 保存，修改 IP 不需要重新编译。也可以用 `LANDSCREEN_SERVER_IP` 和 `LANDSCREEN_SERVER_PORT` 提供首次默认值。

连接设置中的端口必须是 TCP 桥的 `8001`，不能填写 NoMachine 的 `4000`。发送规划请求后，UI 会在断线、发送失败、空路径或 10 秒超时时退出“航线规划中”；点击“取消”会同步清空禁飞数据、旧航线和规划状态。TCP 桥会忽略空闲 socket 超时并继续监听，不再因 60 秒无新连接或无数据而退出。

集成 launch 同时启动 TCP 桥、路径规划、路径校验、视觉消息转换、按格识别状态机和视觉启动器。桥接节点接收 LandScreen 发来的禁区 JSON，并发布到 `/mission/forbidden_zones`。当 `launch=true` 时，它会向 `/mission/command` 发送 `START`；摄像头与 YOLO 在此时启动，规划路径和按格识别结果自动回传 UI。

## 6. 地面端路径规划演示

在机载 NX 上启动集成节点：

```bash
roslaunch nuedc_ground_air landscreen_ros1_bridge.launch
```

数据流如下：

```text
LandScreen -> /mission/forbidden_zones -> ground_path_planner
ground_path_planner -> /planner/path -> onboard_path_receiver
/planner/path -> LandScreen bridge -> Qt map
```

机载接收节点会通过 `/planner/path_ack` 发布路径校验结果。

规划器生成多组确定性覆盖候选，每段使用四邻域 A* 最短路连接，并按总点数和转弯数选优。禁飞格不会出现在航点中，连续航点只会上下或左右移动；为连接未访问区域或返航，路径可能重复经过自由格。桥回传路径时会附带 `forbidden:[{"a":A,"b":B}]`，LandScreen 据此更新标签并标出禁飞格。

当前规划器运行在 NX，因此 UI 必须先连通 NX 才能收到路径。Nano 到 NX 无路由、TCP 8001 未监听或规划节点未启动时，UI 会停在“航线规划中”。

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

如果 YOLO 与摄像头已经由其他终端启动，只启动检测适配器：

```bash
roslaunch nuedc_ground_air onboard_real_vision_demo.launch start_yolo:=false
```

`onboard_real_vision_demo.launch` 是独立视觉调试入口，不会启动 TCP 桥、规划器或按格识别状态机。比赛集成时使用第 5 节的 `landscreen_ros1_bridge.launch`，不要同时运行两个入口中的检测适配器，以免重复发布 `/vision/detections`。

查看带框画面：

```bash
rqt_image_view /yolo_trt_node/annotated
```

适配器将类别编号映射为 `elephant`、`tiger`、`monkey`、`kongque`、`wolf`，并额外发布 JSON 统计 `/vision/summary`。该启动文件会关闭 `fake_yolo_node.py`，避免模拟检测与真实检测同时发布。默认置信度为 `0.60`，默认最多以 `5 Hz` 转发每帧置信度最高的 10 个目标；这些值均可通过 launch 参数修改。

LandScreen 的绿色“发送”按钮只发送禁飞区和任务信息。识别结果由桥接节点自动转发到“显示目标信息”页面。当前只回传类别、置信度和检测框；桥内由像素中心换算场地坐标的逻辑仍是模拟占位，不代表真实目标定位。

2026-07-15 已在机载电脑使用 `/dev/video0` 验证 USB Camera 和 TensorRT YOLO：`/usb_cam/image_raw` 与 `/yolo_trt_node/annotated` 均稳定在约 `30 Hz`，空场景会发布空检测数组和 `{"counts":{},"total":0}`。真实动物正样本识别仍需摆放对应图片或实物验证。

## 闭合覆盖航线

地图按 `A1..A9` 从左到右、`B1..B7` 从下到上显示，红点为 `A9,B1`。规划器只允许上下左右四邻域移动，避开禁飞格，覆盖所有可达格后返回红点。规划时生成多组候选访问顺序，每段使用 A* 最短连接，最后按总步数和转弯数选择闭合路线。无禁飞格时路线为 65 个点；禁飞格为 `A5,B3`~`A5,B5` 时为 61 个点，两者均达到对应闭合覆盖的理论下限。

## 按格识别状态机

`grid_recognition_state_machine.py` 仅处理视觉统计，不读写飞控、MAVROS 或路径执行指令。收到任务 `START` 后，节点等待 `/current_grid` (`std_msgs/String`) 发布 `A1B1` 这样的格子编号，稳定 0.3 秒后统计 1.0 秒。同一帧内同类检测框用于数量统计，多帧之间取单帧最大数，防止按帧累加。已完成格子会被记录，重复经过时不再上报。结果通过 `/vision/grid_result` 传到 LandScreen。

与飞控/定位开发人员的边界只是一个 ROS 话题：在确认航点到达并稳定后，向 `/current_grid` 发布 `std_msgs/String`。话题名可用私有参数 `~current_grid_topic` 或 launch 参数 `current_grid_topic` 配置，也可使用 ROS remap 接到已有的到点话题。视觉节点不订阅 MAVROS，不发布速度、位置或模式切换指令。

在尚未接入定位时可手动测试（只触发识别，不控制无人机）：

```bash
rostopic pub -1 /current_grid std_msgs/String "data: 'A6B1'"
```

## 竞赛参数

- 场地为 9×7 格，每格 `0.5 m`，总尺寸 `4.5 m × 3.5 m`。
- `/planner/path` 默认高度已设为 `1.2 m`，每个航点的 `z=1.2`。真正保持 `120±10 cm` 仍由后续飞控执行模块负责。
- 按格识别默认稳定 `0.3 s`、统计 `1.0 s`，63 格纯视觉时间窗约 `81.9 s`，为 300 秒总时限保留飞行和转弯时间。
- LandScreen 每次任务会在工程目录生成 `animal_results_yyyyMMdd_HHmmss.csv`，保存时间、格子、动物英文名和数量。
- 激光笔是独立硬件执行项，当前视觉/规划代码不切换 GPIO 或飞控辅助通道。

飞控、电机电调、MAVROS、定位、按格握手和安全验收的完整交接说明见 [`docs/无人机飞控负责人对接说明.md`](docs/无人机飞控负责人对接说明.md)。
