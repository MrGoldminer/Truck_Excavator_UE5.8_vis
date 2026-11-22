# 02 · AQuarryVehiclePawn —— 车辆基座

**文件**：`Source/QuarryWorks/Public+Private/Vehicles/QuarryVehiclePawn.{h,cpp}`
**蓝图类型**：`UCLASS(Blueprintable)` —— 可创建子蓝图

## 职责

两台车（挖掘机/矿卡）的公共底座：物理底盘 + 追踪相机 + 统一输入接口。

## 组件结构（构造函数装配）

```
AQuarryVehiclePawn
├─ Chassis (UBoxComponent)            物理刚体：SimulatePhysics + CCD + 低阻尼
├─ VisualRoot (USceneComponent)       视觉根：真机网格/轮组件全挂这下面
├─ VehicleMovement (QuarryVehicleMovementComponent)   力驱动物理
├─ ImuSensor (QuarryImuSensorComponent)               IMU 传感器
└─ CameraBoom → FollowCamera          Horizon 风格滞后跟随相机
```

## 输入接口（键盘与 MQTT 共用同一入口）

| 接口 | 作用 | 键盘绑定 | MQTT 字段 |
|---|---|---|---|
| `SetDriveInput(Throttle, Steering)` | 油门/转向 | W/S/A/D | throttle+steer |
| `SetServiceBrake(Brake)` | 行车制动 | Space | brake |
| `SetParkingBrake(bOn)` | 驻车 | L Shift | hand_brake |

默认驻车：`BeginPlay` 时拉手刹防溜车；`SetupPlayerInputComponent` 时松开。

## 在编辑器中创建

1. Content Browser → 右键 → Blueprint Class → 选 `AQuarryVehiclePawn` 或其子类
2. 命名 `BP_MiningTruck` / `BP_Excavator`
3. 子类构造里已有的组件自动继承；可在编辑器继续调 CameraBoom 参数
4. 放入关卡：拖入或 Place Actors 面板搜索
5. 交付测试：GameMode 的 DefaultPawnClass 指向对应 BP
