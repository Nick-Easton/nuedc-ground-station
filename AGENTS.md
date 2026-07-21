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
| `scripts/ground_path_planner.py` | 根据禁区格生成 `/mission/global_path`。它是后台节点，没有窗口。 |
| `scripts/onboard_path_receiver.py` | 验证收到的路径并发布回执；当前不控制飞行器。 |
| `scripts/fc_state_bridge.py` | 读取 MAVROS 状态、电池和本地位姿，发布 `/drone/state`；不发送飞控命令。 |
| `scripts/vision_detection_adapter.py` | 转换 YOLO 检测并发布 `/vision/detections`、`/vision/summary`；实验性的 `/mission/vision_goal` 默认关闭，显式启用后还需通过 3/5 帧确认、冷却和空间去重。 |
| `scripts/vision_start_on_command.py` | 收到 `START` 后启动相机、YOLO 和检测适配器；收到 `STOP` 或终止状态后释放摄像头。 |
| `LandScreen-master/` | Qt 地图界面和本地假服务器。可执行文件名为 `planescreen`。 |
| `launch/` | ROS 1 启动文件。 |
| `msg/` | ROS 1 自定义消息。修改后必须重新运行 `catkin_make`。 |
| `systemd/`、`tools/install_onboard_user_service.sh` | NX 用户级一体化服务及安装脚本；用于登录后自动启动和异常重启。 |

## 路径规划链路

不要把界面、规划算法和路径执行混为一体：

```text
LandScreen Qt 地图界面
  -> TCP 换行分隔 JSON
landscreen_ros1_bridge
  -> /mission/forbidden_zones
ground_path_planner
  -> /mission/global_path
  -> onboard_path_receiver（仅验证和回执）
  -> landscreen_ros1_bridge（把路径回传到地图显示）
```

关键 topics：

- `/mission/command`：任务命令。
- `/mission/state`：任务状态。
- `/mission/forbidden_zones`：地图选出的最多三个禁区格。
- `/vision/detections`：目标检测。
- `/drone/state`：飞行器状态，目前主要供 PyQt5 面板显示。
- `/mission/global_path`：`nav_msgs/Path`，坐标系必须是 `mission`。
- `/mission/vision_goal`：实验性 `geometry_msgs/PoseStamped`，坐标系为 `base_link`，默认不发布，未完成标定前禁止用于实机控制。
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

比赛集成启动（机载 NX，规划器与桥运行在同一 ROS Master）：

```bash
cd ~/catkin_ws
source /opt/ros/noetic/setup.bash
source ~/catkin_ws_yolo_trt/devel/setup.bash
source devel/setup.bash
roslaunch nuedc_ground_air landscreen_ros1_bridge.launch camera:=/dev/video0
```

地面电脑的 Qt 终端：

```bash
cd ~/catkin_ws/src/nuedc_ground_air/LandScreen-master/build
./planescreen
```

Nano UI 使用“连接设置”保存 NX 地址并通过 TCP 8001 通信，不需要加入 ROS Master。不要把现场 IP 写死在源码中；环境变量只作为首次默认值。

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

- `ground_path_planner.py` 当前只遍历固定的 9×7 网格，生成多组覆盖候选并用四邻域 A* 连接，按点数和转弯数选优；它不是通用连续空间规划器或动态避障规划器，连接和返航时可能重复经过自由格。
- 默认网格间距为 `0.5 m`，比赛航线高度为 `1.2 m`，可通过 launch 参数修改。
- `onboard_path_receiver.py` 只检查 `frame_id`、空路径、点数和有限坐标，不会向飞控发送航点。
- `fc_state_bridge.py` 只读取遥测并发布 `/drone/state`，不会解锁、切换模式、起飞或发送设定值。
- `vision_detection_adapter.py` 发布经过多帧确认的图像相对平移目标；当前没有深度融合、相机标定投影或真实地理坐标估计。
- `fake_yolo_node.py` 是模拟数据源，不代表真实识别效果。
- 桥接节点把图像检测中心按简单比例映射到演示场地，这是占位定位逻辑，不是真实目标地理定位。
- `launch=true` 只在从 false 变为 true 的上升沿触发一次 `START`；回到 false 时触发一次 `STOP`。集成 launch 让按格状态机常驻等待命令，摄像头、YOLO 和检测适配器按任务启停。
- TCP 协议是一行一个 JSON；修改任一端协议时必须同时修改另一端并保留换行分隔。
- 路径回传 JSON 的 `forbidden` 是可选数组，元素格式为 `{"a":1..9,"b":1..7}`；LandScreen 用它同步标签和禁飞格覆盖层，旧客户端可忽略。
- 尚未实现真实飞控桥、真实相机定位、完整安全状态机和实际路径执行闭环。

## 修改规则

1. 开始工作前运行 `git status -sb`，确认没有覆盖队友未提交的更改。
2. 获取进度优先使用 `git pull --ff-only`。如果本地有未提交修改，先提交或暂存，不要强制覆盖。
3. 所有代码和文档更新都先创建独立分支（Codex 默认使用 `codex/` 前缀），提交并推送后创建草稿 PR。必须等待用户或队友明确审核同意，才能将 PR 合并到 `main`；创建 PR 不代表获得合并许可。
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

- 2026-07-17 | `feature/real-yolo` | 审查并整合队友 `agent/ros-yolo-ground-station-integration`：新增实时 `/vision/summary`、UI 识别启停和未定位目标展示；修复按格节点错过 START、后台预览默认开启、工作区加载顺序和终止状态重复 STOP | Python 编译、XML 解析、Git 空白及 START/STOP 模拟生命周期检查通过；队友此前已完成 NX 启停与 Qt 构建验证 | 本轮两台 Jetson 因热点离线，尚未对整合结果重新执行远程 catkin_make 和 Qt 构建。
- 2026-07-17 | `feature/real-yolo` | 修复 Qt 规划状态无法取消/失败不复位、桥空闲约 60 秒退出和 NX 一体化 launch 未部署的问题；新增用户级自动启动服务 | Nano Qt 构建、NX catkin_make、桥持续运行且无重启、TCP 三禁区返回 63 点路径通过 | 手机热点实测平均延迟约 600-825 ms 且有丢包，NoMachine 交互仍受网络质量限制。
- 2026-07-16 | `feature/real-yolo` | 合并 `agent/contest-ground-station-integration`，保留真实 YOLO 参数、只读 MAVROS 参数和禁飞格覆盖层，并接入 A* 闭合覆盖、按格识别、CSV 与连接设置 | Python 编译、launch/package XML、Git 空白和代表性路径约束检查通过 | 当前无法通过 SSH 登录 Jetson，尚未对合并结果重新执行 catkin_make 和 Qt 构建。
- 2026-07-16 | `agent/contest-ground-station-integration` | 汇总比赛参数、A* 闭合覆盖、真实 YOLO、按格识别、五类目标页、CSV、运行时连接设置、桌面快捷方式和一体化 NX launch | Python/XML/路径约束检查；此前已在 Jetson 完成 catkin_make、Qt CMake 构建、TCP/UI、`z=1.2` 和手动 `/current_grid` 验证 | 当前 NX 暂时离线；未修改飞控控制，路径执行、定位和激光仍待飞控负责人完成。
- 2026-07-16 | `feature/connection-settings` | LandScreen 新增可持久保存的 NX IP/端口设置页，保存后立即重连；新增相对路径桌面快捷方式安装脚本 | Qt CMake 构建并在 Nano 截图验证连接状态与设置按钮 | 网络不互通时仍无法请求 NX 上的规划器。
- 2026-07-16 | `feature/contest-parameters` | 按 4.5m×3.5m、0.5m 格长、1.2m 高度和 300s 时限更新规划/按格识别参数；LandScreen 增加每次任务 CSV 落盘 | Python 编译、catkin_make、Qt CMake 构建、`/planner/path` z 值和 CSV 表头验证通过 | 未修改飞控；高度跟踪、速度和激光笔须由后续执行模块落实。
- 2026-07-16 | `feature/grid-recognition` | 新增与飞控完全解耦的按格识别状态机，使用 `/current_grid` 触发稳定/统计时间窗，按单帧最大数去除跨帧重复，已扫描格不重复上报 | Python 编译、ROS 手动 `/current_grid` 触发和状态转换验证通过 | 真实自动触发仍需后续定位节点发布 `/current_grid`；本功能不控制飞控。
- 2026-07-16 | `feature/route-optimization` | 将固定蛇形顺序改为多候选覆盖顺序 + 四邻域 A* 连接，从 `A9,B1` 红点出发并返回；同步修正 LandScreen 按缩放后像素尺寸绘制航线 | Python 编译、Qt CMake 构建、双机 TCP/ROS/UI 闭环通过；无禁飞区 77→65 点，3 个竖直禁飞格 85→61 点 | 仍是固定 9×7 静态格，不处理动态障碍。
- 2026-07-15 | `feature/real-yolo` | 修复覆盖路径删除禁飞航点后仍用直线穿越禁飞格的问题，使用自由网格 BFS 绕行并在 LandScreen 标记禁飞格 | Python 约束检查通过；双机生成 69 点路径、覆盖 60 个自由格、禁飞格访问为 0、Qt 两次完成 69 点重绘 | 仍是固定 9×7 静态网格，不处理动态障碍。
- 2026-07-15 | `feature/real-yolo` | 接入 USB Camera + `yolo_trt_ros` 检测适配和集成启动，并让 LandScreen 支持运行时服务器地址 | 两台 Ubuntu Python/XML 检查与 `catkin_make`、Qt 构建、模拟 `vision_msgs -> TCP -> UI` 通过；真实 `/dev/video0` 原图与带框图约 30 Hz | 空场景检测与通信正常，真实动物正样本识别尚待验证。
- 2026-07-15 | `feature/fc-bridge` | 在真实双机环境部署飞控遥测桥，验证地面 `/planner/path` 到机载回执，并尝试 CP2102N 串口 MAVROS | 两台 Ubuntu `catkin_make` 通过；60 点路径回执成功；MAVROS 可打开 `/dev/ttyUSB0` | `57600/115200/460800/921600` 原始串口输入均为 0，待检查 PX4 `TELEM2` 接线、供电和 MAVLink 参数。
- 2026-07-15 | `feature/fc-bridge` | 新增 MAVROS/PX4 只读遥测桥和可配置启动文件，统一发布 `/drone/state` | Python 编译、ROS XML 解析和 Git 空白检查通过 | 尚未使用真实 Pixhawk 验证串口、波特率和 MAVLink 心跳。
- 2026-07-15 | `main` | 将 `README.md` 的标题和说明文字翻译为中文，保留命令、topic 和网络示例 | Markdown 差异与空白检查通过 | 示例 IP 仍需按实际网络替换。
- 2026-07-15 | `main` | 新增根目录 Codex 协作说明，记录架构、启动顺序、占位实现、验证要求和 Git 协作规则 | 文档结构与 Git 空白检查通过 | 后续接口或项目状态变化时必须持续更新本文件。
- 2026-07-15 | `main` | 创建 GitHub 私有仓库，提交 ROS 1 演示、LandScreen 桥接、固定网格规划和路径验证基线 | Python 语法、ROS XML/launch 格式检查通过 | 未在本机完成 ROS/Qt 全量构建。
