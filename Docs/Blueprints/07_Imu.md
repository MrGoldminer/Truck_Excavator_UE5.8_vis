# 07 · UQuarryImuSensorComponent —— IMU 传感器

**文件**：`Source/QuarryWorks/Public+Private/Sensors/QuarryImuSensorComponent.{h,cpp}`
**蓝图类型**：`BlueprintSpawnableComponent`

## 输出（CARLA IMU 语义）

| 输出 | 单位 | 帧 | 说明 |
|---|---|---|---|
| GetSpecificForceMps2 | m/s² | 车体系 | 比力 = a − g（静止读 +1g 向上） |
| GetAngularVelocityRadS | rad/s | 车体系 | 陀螺仪角速度 |
| GetWorldAccelerationMps2 | m/s² | 世界系 | 世界加速度 |
| GetCompassRadians | rad [0,2π) | — | 0 = +X 北 |

## 采样机制

- 固定频率（`SampleRateHz`，默认 100Hz），帧内累积器触发
- 加速度差分法（Δv/Δt）→ 对帧率敏感，故采样率固定
- 白噪声模型：均匀分布和近似高斯 + 常值偏置（`AccelerometerBiasMps2` 等可配）

## 用途

- RL/自主驾驶栈的传感器输入（经 MQTT JSON 的 `imu` 字段下发）
- HUD 的 `IMU |f|` 读数（静止 = 9.8 ✓ 已验证）
