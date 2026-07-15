# Codex 协作与进度说明

本文件是本仓库中面向 Codex 和协作者的首要上下文。开始修改前请完整阅读；完成修改后，如果项目状态、启动方式、接口或限制发生变化，请同步更新本文件和 `README.md`。

## 项目定位

- 项目名称：NUEDC ROS 1 地面站与空地通信起步工程。
- ROS 包名：`nuedc_ground_air`。
- 目标环境：Ubuntu + ROS 1（ROS Noetic 为主要参考环境）。
- 默认分支：`main`，远程仓库为 `Nick-Easton/nuedc-ground-station`。
- Qt 地图地面站通过 TCP JSON 与机载 ROS 1 桥接节点通信，不是 ROS 2 程序。

## 当前基线（2026-07-15）

当前仓库已经实现：

1. ROS 1 自定义消息、任务命令和任务状态演示链路。
2. 假 YOLO 检测数据发布节点。
3. PyQt5 状态面板，可发送 `START`、`STOP`、`ABORT` 命令。
4. NJUPT `LandScreen` Qt 地图界面的 Ubuntu 构建支持。
5. `LandScreen` 与 ROS 1 之间的 TCP JSON 桥接。
6. 根据最多三个禁区格生成往复式覆盖路径的地面端规划节点。
7. 机载路径接收和基础合法性检查，以及 `/planner/path_ack` 回执。
8. Git 仓库初始化、Linux 换行规则、Python 可执行权限和构建产物忽略规则。
9. 通过 MAVROS 读取 PX4 飞控状态，并统一发布 `/drone/state`；当前仅遥测，不发送控制命令。
10. 将 `yolo_trt_ros` 的 `vision_msgs/Detection2DArray` 转换为统一检测 topic，并通过现有 TCP 桥回传 LandScreen。

首次 GitHub 基线提交为 `d11dd1e`（`Initial ground station implementation`）。后续进度以 `main` 上的实际提交为准，不要在代码中依赖该提交号。

## 组件边界

| 路径 | 职责 |
| --- | --- |
| `scripts/mission_node.py` | 接收任务命令，维护演示任务状态。 |
| `scripts/fake_yolo_node.py` | 发布模拟目标检测；后续应由真实相机与 YOLO 节点替换。 |
| `scripts/ground_station_cli.py` | 终端版任务控制和状态显示。 |
| `scripts/ground_station_gui.py` | PyQt5 状态面板；它不是路径地图界面。 |
| `scripts/landscreen_ros1_bridge.py` | TCP 8001 与 ROS topics 之间的 JSON 桥。 |
| `scripts/ground_path_planner.py` | 根据禁区格生成 `/planner/path`。它是后台节点，没有窗口。 |
| `scripts/onboard_path_receiver.py` | 验证收到的路径并发布回执；当前不控制飞行器。 |
| `scripts/fc_state_bridge.py` | 读取 MAVROS 状态、电池和本地位姿，发布 `/drone/state`；不发送飞控命令。 |
| `scripts/vision_detection_adapter.py` | 将 `/yolo_trt_node/detections` 转换为 `/vision/detections` 和 `/vision/summary`，并限制转发速率和单帧数量。 |
| `LandScreen-master/` | Qt 地图界面和本地假服务器。可执行文件名为 `planescreen`。 |
| `launch/` | ROS 1 启动文件。 |
| `msg/` | ROS 1 自定义消息。修改后必须重新运行 `catkin_make`。 |

## 路径规划链路

不要把界面、规划算法和路径执行混为一体：

```text
LandScreen Qt 地图界面
  -> TCP 换行分隔 JSON
landscreen_ros1_bridge
  -> /mission/forbidden_zones
ground_path_planner
  -> /planner/path
  -> onboard_path_receiver（仅验证和回执）
  -> landscreen_ros1_bridge（把路径回传到地图显示）
```

关键 topics：

- `/mission/command`：任务命令。
- `/mission/state`：任务状态。
- `/mission/forbidden_zones`：地图选出的最多三个禁区格。
- `/vision/detections`：目标检测。
- `/drone/state`：飞行器状态，目前主要供 PyQt5 面板显示。
- `/planner/path`：`nav_msgs/Path`，坐标系必须是 `map`。
- `/planner/path_ack`：机载接收器的 JSON 字符串回执。
- `/mavros/state`、`/mavros/battery`、`/mavros/local_position/pose`：MAVROS 原始飞控遥测输入。
- `/yolo_trt_node/detections`：真实 TensorRT YOLO 发布的 `vision_msgs/Detection2DArray`。
- `/vision/summary`：适配器发布的单帧类别计数 JSON。

## 正确启动顺序

### 完整 LandScreen 演示

机载电脑：

```bash
cd ~/catkin_ws
catkin_make
source devel/setup.bash
roslaunch nuedc_ground_air onboard_landscreen_demo.launch
```

地面电脑的 ROS 终端：

```bash
cd ~/catkin_ws
source devel/setup.bash
roslaunch nuedc_ground_air ground_path_planner.launch
```

地面电脑的 Qt 终端：

```bash
cd ~/catkin_ws/src/nuedc_ground_air/LandScreen-master/build
LANDSCREEN_SERVER_IP=YOUR_ONBOARD_IP LANDSCREEN_SERVER_PORT=8001 ./planescreen
```

两台电脑必须指向同一个 ROS master，并分别正确设置 `ROS_MASTER_URI` 和 `ROS_IP`。不要把示例地址 `172.20.10.11`、`172.20.10.9` 或 Qt 源码默认地址 `192.168.10.3` 当成固定配置。

### 只测试 Qt 界面

终端 1：

```bash
cd LandScreen-master
python3 tools/fake_landscreen_server.py
```

终端 2：

```bash
cd LandScreen-master/build
LANDSCREEN_SERVER_IP=127.0.0.1 LANDSCREEN_SERVER_PORT=8001 ./planescreen
```

### 只读飞控遥测

机载电脑通过 USB 连接 PX4/Pixhawk 时：

```bash
roslaunch nuedc_ground_air onboard_fc_telemetry.launch \
  fcu_url:=/dev/ttyACM0:57600
```

如果 MAVROS 已经由其他工程启动：

```bash
roslaunch nuedc_ground_air onboard_fc_telemetry.launch start_mavros:=false
```

### 真实视觉通信

启动 USB 摄像头、TensorRT YOLO、ROS/TCP 桥和视觉适配器：

```bash
source /opt/ros/noetic/setup.bash
source ~/catkin_ws_yolo_trt/devel/setup.bash
source ~/catkin_ws/devel/setup.bash
roslaunch nuedc_ground_air onboard_real_vision_demo.launch camera:=/dev/video0 show_window:=false
```

YOLO 已在其他终端运行时：

```bash
roslaunch nuedc_ground_air onboard_real_vision_demo.launch start_yolo:=false
```

## 当前限制和占位实现

修改或解释代码时必须明确以下限制：

- `ground_path_planner.py` 当前只遍历固定的 9×7 网格，按列往返生成覆盖路径，并跳过最多三个禁区格；它不是通用 A*、Dijkstra 或动态避障规划器。
- 默认网格间距为 `0.5 m`，默认高度为 `2.0 m`，可通过 launch 参数修改。
- `onboard_path_receiver.py` 只检查 `frame_id`、空路径、点数和有限坐标，不会向飞控发送航点。
- `fc_state_bridge.py` 只读取遥测并发布 `/drone/state`，不会解锁、切换模式、起飞或发送设定值。
- `vision_detection_adapter.py` 只转换检测框消息；当前没有深度融合、相机标定投影或真实地理坐标估计。
- `fake_yolo_node.py` 是模拟数据源，不代表真实识别效果。
- 桥接节点把图像检测中心按简单比例映射到演示场地，这是占位定位逻辑，不是真实目标地理定位。
- `launch=true` 只在从 false 变为 true 的上升沿触发一次 `START`。
- TCP 协议是一行一个 JSON；修改任一端协议时必须同时修改另一端并保留换行分隔。
- 尚未实现真实飞控桥、真实相机定位、完整安全状态机和实际路径执行闭环。

## 修改规则

1. 开始工作前运行 `git status -sb`，确认没有覆盖队友未提交的更改。
2. 获取进度优先使用 `git pull --ff-only`。如果本地有未提交修改，先提交或暂存，不要强制覆盖。
3. 较大的功能使用独立分支，例如 `feature/real-yolo`、`feature/fc-bridge`；通过 PR 合并到 `main`。
4. 不要提交 `build/`、`devel/`、`install/`、日志、IDE 配置、压缩包或密钥；遵守根目录 `.gitignore`。
5. 保持 Python 和 ROS/CMake 文件为 LF 换行，并保留 Python 脚本可执行权限。
6. 不要硬编码个人电脑路径、Wi-Fi 地址或机载 IP。使用环境变量、ROS 参数或 launch 参数。
7. 不要无理由把项目迁移到 ROS 2，也不要把 `LandScreen-master` 当成 ROS 包。
8. 修改 `msg/`、topic 名称、TCP JSON 字段、坐标转换或启动顺序时，必须同步更新 `README.md` 和本文件。
9. 不要删除或重写与当前任务无关的队友更改。发现混合改动时先说明范围。
10. 未经明确要求，不要执行 `git push --force`、重写公共历史或删除远程分支。

## 最低验证要求

提交前至少执行与改动相关的检查：

```bash
python3 -m compileall -q scripts LandScreen-master/tools
cd ~/catkin_ws
catkin_make
```

如果修改 Qt 界面，再执行：

```bash
cd LandScreen-master/build
cmake ..
cmake --build . -j
```

如果有可用 ROS 环境，检查关键节点和 topics：

```bash
rosnode list
rostopic list
rostopic echo /planner/path_ack
```

无法运行某项验证时，在提交说明或 PR 中明确写出原因，不要声称已经通过。

## 进度记录格式

当功能状态发生变化时，在本节顶部增加一条简短记录：

```text
YYYY-MM-DD | 作者/分支 | 变更摘要 | 已执行的验证 | 已知问题
```

当前记录：

- 2026-07-15 | `feature/real-yolo` | 接入 USB Camera + `yolo_trt_ros` 检测适配和集成启动，并让 LandScreen 支持运行时服务器地址 | 两台 Ubuntu Python/XML 检查与 `catkin_make`、Qt 构建、模拟 `vision_msgs -> TCP -> UI` 日志验证通过 | `/dev/video0` 当前未连接，真实相机推理尚未验证。
- 2026-07-15 | `feature/fc-bridge` | 在真实双机环境部署飞控遥测桥，验证地面 `/planner/path` 到机载回执，并尝试 CP2102N 串口 MAVROS | 两台 Ubuntu `catkin_make` 通过；60 点路径回执成功；MAVROS 可打开 `/dev/ttyUSB0` | `57600/115200/460800/921600` 原始串口输入均为 0，待检查 PX4 `TELEM2` 接线、供电和 MAVLink 参数。
- 2026-07-15 | `feature/fc-bridge` | 新增 MAVROS/PX4 只读遥测桥和可配置启动文件，统一发布 `/drone/state` | Python 编译、ROS XML 解析和 Git 空白检查通过 | 尚未使用真实 Pixhawk 验证串口、波特率和 MAVLink 心跳。
- 2026-07-15 | `main` | 将 `README.md` 的标题和说明文字翻译为中文，保留命令、topic 和网络示例 | Markdown 差异与空白检查通过 | 示例 IP 仍需按实际网络替换。
- 2026-07-15 | `main` | 新增根目录 Codex 协作说明，记录架构、启动顺序、占位实现、验证要求和 Git 协作规则 | 文档结构与 Git 空白检查通过 | 后续接口或项目状态变化时必须持续更新本文件。
- 2026-07-15 | `main` | 创建 GitHub 私有仓库，提交 ROS 1 演示、LandScreen 桥接、固定网格规划和路径验证基线 | Python 语法、ROS XML/launch 格式检查通过 | 未在本机完成 ROS/Qt 全量构建。
