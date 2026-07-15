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
3. 添加 `fc_bridge_node`，读取飞控反馈。
4. 在安全逻辑经过充分测试后，再增加低速控制设定值输出。
5. 继续完善现有 PyQt 地面站界面，或增加 rqt 界面。

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
