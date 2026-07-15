# 2025 H 题野生动物巡查系统复现路线

来源工程：

- https://oshwhub.com/2025-electric-race-team-njupt/wu-ren-ji-kuo-zhan-ban-esp32_yang
- 已获取附件：`地面站代码.zip`

## 1. 原团队系统分工

原团队的系统不是单一程序，而是 4 个部分协同：

1. 机载上位机
   - Ubuntu 22.04
   - ROS2 Humble
   - 运行 SLAM、路径规划、飞控桥接、视觉结果融合

2. 飞控
   - Pixhawk 6C 一类飞控
   - 负责姿态、位置控制、任务执行
   - 机载上位机通过 MAVLink / ROS2 桥接发送控制指令

3. 视觉端
   - MaixCAM
   - YOLO11n INT8 模型
   - 输出动物类别、目标位置、检测结果

4. 地面站
   - 树莓派 + HDMI 屏 + 按键/触摸输入
   - Qt6 Widgets 图形界面
   - TCP Socket 与机载上位机通信
   - JSON 按行收发任务配置、路径、动物统计

## 2. 地面站操作流程

地面站开源代码对应的是比赛中的人机交互端，主要操作如下：

1. 打开地面站程序，界面显示 9 x 7 方格地图。
2. 根据现场给出的禁飞区方格代码，分别点击 A 行和 B 行按钮。
3. 每次选择一组 A/B 后，界面填入一个禁飞区，最多支持 3 组。
4. 点击“发送”，地面站把禁飞区 JSON 发给机载上位机。
5. 机载上位机根据禁飞区进行全覆盖路径规划。
6. 机载端把规划出的路径点通过 JSON 回传。
7. 地面站在地图上画出巡查航线。
8. 点击“启动”，地面站发送 `launch=true`。
9. 无人机按路径飞行，机载视觉持续识别动物。
10. 机载端把目标坐标、动物名称、数量回传地面站。
11. 地面站实时更新目标汇总和目标详情页。

## 3. 地面站通信协议

当前开源地面站使用 TCP 客户端连接机载端：

- 默认 IP：`192.168.10.3`
- 默认端口：`8001`
- 每条消息以换行符结尾
- 数据格式为 JSON

地面站发送示例：

```json
{"f1x":2,"f1y":3,"f2x":-1,"f2y":-1,"f3x":-1,"f3y":-1,"launch":false}
```

含义：

- `f1x/f1y`：第 1 个禁飞区方格
- `f2x/f2y`：第 2 个禁飞区方格
- `f3x/f3y`：第 3 个禁飞区方格
- `launch`：是否开始任务

机载端回传示例：

```json
{"planner":[{"x":0.5,"y":0.5},{"x":1.5,"y":1.0}],"tx":1.2,"ty":2.1,"tn":"elephant"}
```

含义：

- `planner`：路径规划结果
- `tx`：目标全局 x 坐标
- `ty`：目标全局 y 坐标
- `tn`：目标名称

## 4. Ubuntu 复现顺序

第一阶段：只复现地面站界面

1. 准备 Ubuntu 22.04。
2. 安装 Qt6、CMake、编译工具。
3. 编译 `LandScreen-master`。
4. 启动本地假机载端 `tools/fake_landscreen_server.py`。
5. 启动地面站，验证禁飞区发送、路径显示、目标统计。

第二阶段：复现机载通信端

1. 写一个 TCP Server，监听 `8001`。
2. 接收地面站禁飞区 JSON。
3. 把禁飞区转换成路径规划输入。
4. 生成或转发 `planner` 路径 JSON。
5. 把视觉识别结果转换成 `tx/ty/tn` JSON。

第三阶段：迁移到 ROS1

1. 建 ROS1 catkin 工作空间。
2. 建议拆成这些节点：
   - `landscreen_ros1_bridge`：TCP JSON 与 ROS1 topic 互转
   - `planner_node`：禁飞区输入，全覆盖路径输出
   - `vision_fusion_node`：动物识别结果与位姿融合
   - `mission_node`：任务状态机
   - `fc_bridge_node`：飞控通信
3. 地面站保持 Qt 程序，不直接依赖 ROS。
4. 机载上位机负责 ROS1 与 TCP 协议桥接。

第四阶段：接真实硬件

1. 树莓派运行 Qt 地面站。
2. 机载 Ubuntu 主机运行 ROS2。
3. MaixCAM 输出识别结果。
4. 激光雷达 + Point-LIO 输出定位结果。
5. 飞控执行路径。
6. 最后再做起飞、巡查、目标照射、45 度降落。

## 5. 当前项目已经做的准备

已加入：

- `LandScreen-master/`：原地面站代码
- `LandScreen-master/README_UBUNTU.md`：Ubuntu 编译运行说明
- `LandScreen-master/tools/fake_landscreen_server.py`：本地假机载端
- `scripts/landscreen_ros1_bridge.py`：ROS1 版地面站桥接节点
- `launch/onboard_landscreen_demo.launch`：ROS1 地面站联调演示启动文件

已调整：

- CMake 不再写死本地 Qt 安装路径
- 地面站连接 IP/端口可用环境变量覆盖
- 发送数据时避免未连接状态下重复写 socket

## 6. 下一步

为了贴近原团队操作，下一步优先做：

1. 在 Ubuntu 22.04 上跑通 `LandScreen-master`。
2. 用假机载端验证地面站收发。
3. 保留 Qt 地面站协议，机载侧统一改成 ROS1。
4. 如果能拿到 `飞机上位机代码.zip`，再把他们的 ROS2 节点逻辑迁移成 ROS1 节点。
