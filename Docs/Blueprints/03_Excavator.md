# 03 · AQuarryExcavator —— 轮式挖掘机

**文件**：`Source/QuarryWorks/Public+Private/Vehicles/QuarryExcavator.{h,cpp}`
**蓝图类型**：`UCLASS(Blueprintable)`

## 职责

轮式挖掘机：行驶（轮式驱动，前轮转向）+ 铰接臂运动（动臂/小臂/铲斗）+ 挖掘力学。

## 组件结构

```
AQuarryExcavator (继承 AQuarryVehiclePawn)
├─ UpperStructurePivot      上部结构回转轴（Q/E 控制 SlewAngle）
│   ├─ BoomPivot            动臂铰点（R/F 控制 BoomAngle）
│   │   └─ StickPivot       小臂铰点（T/G 控制 StickAngle）
│   │       └─ BucketPivot  铲斗铰点（Y/H 控制 BucketAngle）
│   │           └─ BucketTip 挖掘采样点（AQuarryExcavator::GetDigSampleLocation）
├─ RealVehicleMesh          真实分件装配（车身/门/4 轮/动臂/小臂/铲斗）
└─ BlockoutVisuals          黄色方块件（真机加载后全部隐藏）
```

## 挖掘力学

```
挖掘效率 = 基础速率 × 速度因子 × 切入角因子 × 材料硬度
  速度因子 = clamp(尖端速度 / OptimalDigSpeed, 0..1)——静止不挖料
  切入角因子 = 1 − |pitch − OptimalCutAngle| / 容差
  材料硬度 = 料堆的 MaterialHardness（沙 1.0 / 砾石 0.7 / 矿 0.45）
采样位置 = GetDigSampleLocation()（真实 FK 铲斗尖端）
```

## 关键函数

| 函数 | 职责 |
|---|---|
| `Tick` | 积分三个关节角度 + FK 尖端速度追踪 + `TickWork` |
| `StartDigging` / `StopDigging` | 挖掘状态机进入/退出（校验静止/容量/料堆距离） |
| `StartDumping` / `StopDumping` | 卸料到附近矿卡车斗 |
| `FindNearestPile` / `FindNearestTruck` | 作业半径内的目标搜索 |
| `GetDigSampleLocation` | FK 铲斗尖端优先，方块 Tip 回退 |
| `ApplyActuatorCommands` | MQTT 远程执行器控制（slew/boom/stick/bucket） |

## 遥控接口（MQTT `{"type": ..., "excavator": {...}}`）

```json
{"vehicle_id":"excavator","throttle":0.3,"steer":0.1,
 "excavator":{"slew":0.5,"boom":-0.3,"stick":0.4,"bucket":0.2,"dig":true}}
```

## 在编辑器中创建

1. Blueprint Class → 父类 `AQuarryExcavator` → 命名 `BP_Excavator`
2. 细节面板可调：`SlewSpeedDegreesPerSecond`（回转速度）、`HydraulicSpeedDegreesPerSecond`（油缸速度）、`WorkRadius`（作业半径）、`OptimalCutAngleDeg`（最优切入角）
3. 放入关卡后 `AQuarryWorksite::InitializeWorksite` 会自动注册
