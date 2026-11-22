# CARLA 对齐说明（CARLA Alignment）

本项目是独立的 UE 5.8 采石场仿真，**不是** CARLA 的分叉或重实现。本文逐字段说明本项目的对外接口如何对齐 CARLA 的语义、单位与坐标系，以及已知差异。对齐层入口：MQTT JSON（管理面，默认 `quarryworks/v1/` 前缀）与 TCP protobuf（`Proto/quarryworks.proto`）。

物理方法论同源：CARLA 车辆同样使用 raycast 式悬架 + 滑移轮胎模型（Bullet 模式为简化 Pacejka/魔方公式 + 摩擦圆）。本项目的 `UQuarryVehicleMovementComponent` 采用相同架构。

## 1. 控制（carla.VehicleControl → quarryworks command）

| carla 字段 | 本项目 MQTT JSON 字段 | 单位/范围 | 映射 |
|---|---|---|---|
| `throttle` | `throttle` | [0, 1] | 正向油门 |
| `steer` | `steer` | [-1, 1] | 左负右正（同 CARLA） |
| `brake` | `brake` | [0, 1] | 服务制动 |
| `hand_brake` | `hand_brake` | bool | 驻车制动 |
| `reverse` | `reverse` | bool | true 时 throttle 反向映射为倒挡 |
| — | `vehicle_id` | string | `excavator` / `mining_truck` |

示例：`{"vehicle_id":"mining_truck","throttle":0.6,"steer":-0.2,"brake":0.0,"hand_brake":false,"reverse":false}`

TCP protobuf 路径保持原有带符号油门语义（throttle -1..1，负值倒车），两路命令在 tick 边界统一应用。

## 2. 传感器语义（MQTT `.../telemetry`）

| carla 概念 | 本项目字段 | 单位 | 帧 |
|---|---|---|---|
| `carla.Actor.get_location()` | `vehicles[].location.{x,y,z}` | 米 | 世界（UE 厘米 ÷ 100） |
| `carla.Actor.get_velocity()` | `vehicles[].velocity.{x,y,z}` | m/s | 世界 |
| `carla.Actor.get_transform()` | 位置 + `imu.compass` 推导航向 | — | — |
| `carla.IMUMeasurement.accelerometer` | `vehicles[].imu.accelerometer.{x,y,z}` | m/s²（比力，已去重力） | **车体系**（CARLA 为世界系，见差异 ①） |
| `carla.IMUMeasurement.gyroscope` | `vehicles[].imu.gyroscope.{x,y,z}` | rad/s | **车体系**（同差异 ①） |
| `carla.IMUMeasurement.compass` | `vehicles[].imu.compass` | rad，[0, 2π)，0=北 | 北 = 世界 +X（CARLA 以地图为准，见差异 ②） |
| GNSS（`carla.GnssMeasurement`） | `vehicles[].gnss.{lat,lon,alt}` | 度 / 米 | 可配置原点（`GeoOriginLat/Lon`）+ 等距投影 |
| 速度计 | `vehicles[].speed_kmh` | km/h | 速率模 |
| `carla.WheelPhysicsControl` 观测量 | `vehicles[].wheels[]`：`slip_ratio, slip_angle_rad, load_n, in_contact` | — / rad / N / — | 每轮，N 为法向载荷 |

## 3. 已知差异（Deltas）

1. **IMU 数据帧**：CARLA 的 accelerometer/gyroscope 报告世界系分量；本项目额外提供车体系（对 IMU 物理更正确）。如需世界系，可用 `orientation`（TCP protobuf `orientation_quat`）自行旋转。两者数值模长一致。
2. **北向定义**：CARLA 北向随地图摆放；本项目固定北 = 世界 +X，GNSS 原点与北向均可通过 `Config/DefaultGame.ini [QuarryWorks]` 调整，使投影与真实地图对齐。
3. **无 RPC / actor 生命周期 API**：本项目固定两台工程车，无 spawn/destroy；控制与任务经由 MQTT/TCP 消息完成。
4. **无交通流 / 语义分割等渲染传感器**：本仿真聚焦工程车辆动力学与作业（挖掘/运输）。
5. **物理细节**：悬架/轮胎参数按矿卡与挖掘机标定（而非 CARLA 小客车），参数暴露在 pbtxt/ini 中可调。
6. **任务面**：`.../task`（drive_to / dig / haul / test_suite）为管理面扩展，CARLA 无对应概念；结果同步回发 `.../task/result` 与 `.../test/result`。

## 4. MQTT 主题一览

| 主题 | 方向 | QoS | 说明 |
|---|---|---|---|
| `quarryworks/v1/telemetry` | 仿真 → 管理端 | 0 | 20Hz CARLA 命名 JSON 遥测 |
| `quarryworks/v1/command` | 管理端 → 仿真 | 1 | VehicleControl JSON |
| `quarryworks/v1/task` | 管理端 → 仿真 | 1 | 任务下发 |
| `quarryworks/v1/task/result` | 仿真 → 管理端 | 0 | 任务完成/失败（含 elapsed、delivered） |
| `quarryworks/v1/test/result` | 仿真 → 管理端 | 0 | 测试用例逐条 + 最终 summary |
| `quarryworks/v1/status` | 仿真 → 管理端 | 0 | retained online/offline（LWT） |

连接参数：`Config/DefaultGame.ini [QuarryWorks]`（MqttHost/MqttPort/MqttPrefix/MqttTelemetryRateHz）。控制台：`Quarry.Mqtt.Start` / `Quarry.Mqtt.Stop`。
