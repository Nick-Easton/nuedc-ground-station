# D 题陆空监控 UI 与激光雷达数据接口

## 1. 功能边界

本 UI 是只读监控端，负责：

- 按 `field` 坐标系显示 400 cm × 500 cm 场地、H/A/B/C/D、固定轨迹；
- 以 10 Hz 左右刷新小车和无人机的位置、航向、轨迹和基础参数；
- 用中文显示起飞、悬停、捕获小车、伴飞、抛投、动态下降、返航、降落等状态；
- 显示定位有效性、车/机链路、视觉目标、数据龄和关键状态变化日志。

它不发布小车启停、速度、转向或无人机控制命令，也不参与任何实时控制闭环。

## 2. 坐标约定

- 坐标系：`field`；
- 原点：场地左下角；
- `+x`：向右，范围 `0.0 ~ 4.0 m`；
- `+y`：向上，范围 `0.0 ~ 5.0 m`；
- 航向：角度制，`0°` 指向 `+x`，逆时针为正；
- H 点：`(0.75, 0.75)`；
- A/B/C/D：`(1.50,2.00)`、`(1.50,3.50)`、`(3.00,3.50)`、`(3.00,2.00)`。

雷达或 SLAM 输出如果不是这个坐标系，必须在机载适配器之前完成外参标定和坐标变换，UI 不负责猜测或修正坐标。

## 3. 数据链路

```text
激光雷达定位节点
  -> /lidar/car/odom          nav_msgs/Odometry
  -> /lidar/drone/odom        nav_msgs/Odometry
ground_air_telemetry_adapter.py（只读归一化）
  -> /ground_station/telemetry std_msgs/String(JSON)
landscreen_ros1_bridge.py
  -> TCP 8001，每行一个 JSON
Qt planescreen 监控 UI
```

实际 topic 名可以通过 launch 参数替换，不要在源码中写死现场命名：

```bash
roslaunch nuedc_ground_air ground_air_monitor.launch \
  car_odometry_topic:=/your_lidar/car/odom \
  drone_odometry_topic:=/your_lidar/drone/odom \
  mission_mode:=DROP
```

## 4. TCP JSON 协议 v1

一行一个 UTF-8 JSON，每行必须以 `\n` 结束：

```json
{
  "type": "ground_air_telemetry",
  "timestamp_ms": 1785398400123,
  "source": "lidar",
  "latency_ms": 23,
  "car": {
    "valid": true,
    "x_m": 1.50,
    "y_m": 2.42,
    "yaw_deg": 90.0,
    "speed_mps": 0.12,
    "battery_percent": 86.0
  },
  "drone": {
    "valid": true,
    "x_m": 1.48,
    "y_m": 2.35,
    "z_m": 1.50,
    "yaw_deg": 89.2,
    "speed_mps": 0.12,
    "battery_percent": 91.0,
    "armed": true,
    "flight_mode": "OFFBOARD",
    "target_visible": true,
    "target_confidence": 0.94
  },
  "mission": {
    "mission_id": "D-01",
    "mode": "DROP",
    "state": "FOLLOW_CAR",
    "elapsed_s": 13.7,
    "drop_done": false,
    "touchdown_confirmed": false
  },
  "links": {
    "localization_ok": true,
    "car_link_ok": true,
    "drone_link_ok": true
  }
}
```

`mission.state` 支持：

| 状态码 | UI 中文 |
| --- | --- |
| `IDLE` | 任务待机 |
| `TAKEOFF` / `TAKEOFF_1P5M` | 正在起飞 |
| `HOVER` / `HOVER_3S` | 稳定悬停 |
| `ACQUIRE_CAR` | 搜索并捕获小车 |
| `FOLLOW_CAR` | 伴飞中 |
| `ALIGN_AND_DROP` / `DROP` | 对准投放点 / 正在抛投 |
| `DYNAMIC_DESCENT` / `TOUCHDOWN` / `HOLD_ON_CAR` | 动态下降 / 接触确认 / 随车停留 |
| `RE_TAKEOFF` / `RETURN_HOME` / `RTL` | 再次起飞 / 返航 |
| `LANDING` / `LANDED` / `COMPLETE` | 降落 / 已降落 / 任务完成 |
| `ABORT` / `ERROR` / `FAILED` | 中止 / 故障 / 失败 |

## 5. 离线验收

不需要 ROS 或雷达即可测试 TCP 链路：

```bash
cd LandScreen-master
python3 tools/fake_ground_air_server.py
```

另一个终端启动 `build/planescreen`，连接 `127.0.0.1:8001`。也可以直接点击 UI 中的“演示数据”，该模式完全在 Qt 进程内生成数据。

截图或窗口化调试时可用：

```bash
GROUND_AIR_MONITOR_WINDOWED=1 GROUND_AIR_MONITOR_DEMO=1 ./build/planescreen
```

协议检查：

```bash
python3 tools/test_ground_air_protocol.py
```

## 6. 真雷达接入前必须实测

- `field` 与雷达地图的平移、旋转、尺度和时间戳；
- 雷达同时区分小车、无人机目标的方法，以及遮挡时 `valid=false` 的行为；
- 10 Hz 显示条件下端到端数据龄，UI 超时阈值当前为 1.2 秒；
- 无人机高度来自雷达、飞控还是融合节点，避免重复坐标系；
- 场外坐标、NaN、断链、乱序和进程重启时是否正确告警。

当前提交只验证了接口和模拟数据，不代表真实激光雷达定位、ROS topic、Qt/Nano 实机运行已经通过。
