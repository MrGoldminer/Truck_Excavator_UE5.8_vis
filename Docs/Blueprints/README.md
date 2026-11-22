# 蓝图文档索引

本目录存放每个 C++ 模块对应的蓝图解释文档与编辑器构建步骤。

| 文档 | 对应 C++ | 内容 |
|---|---|---|
| [01_VehicleMovement.md](01_VehicleMovement.md) | QuarryVehicleMovementComponent | 力驱动物理：悬架/轮胎/动力总成 |
| [02_VehiclePawn.md](02_VehiclePawn.md) | QuarryVehiclePawn | 两台车的公共基座与输入接口 |
| [03_Excavator.md](03_Excavator.md) | QuarryExcavator | 轮式挖掘机：铰接臂 + 挖掘力学 |
| [04_MiningTruck.md](04_MiningTruck.md) | QuarryMiningTruck | 矿卡：货运 + 倾卸 |
| [05_VehicleMesh.md](05_VehicleMesh.md) | QuarryVehicleMeshComponent | 真实资产绑定（三路分发） |
| [06_DigSite_Materials.md](06_DigSite_Materials.md) | QuarryDigSite / QuarryMaterialPile / QuarryCargoComponent | 可变形地形 + 料流 |
| [07_Imu.md](07_Imu.md) | QuarryImuSensorComponent | IMU 传感器 |
| [08_Mqtt_Network.md](08_Mqtt_Network.md) | QuarryMqttClient / QuarryTcpServer / QuarryJsonCodec | MQTT 管理面 + TCP 桥 |
| [09_TaskExecutor.md](09_TaskExecutor.md) | QuarryTaskExecutor | 任务执行 + 测试套件 |
| [10_Worksite_HUD.md](10_Worksite_HUD.md) | QuarryWorksiteSubsystem / AQuarryWorksite / AQuarryHUD / 配置子系统 | 场景装配 + HUD + 配置 |

**总构建指南**：[../BLUEPRINT_BUILD_GUIDE.md](../BLUEPRINT_BUILD_GUIDE.md) —— 从空场景到完整采石场的编辑器逐步操作。
