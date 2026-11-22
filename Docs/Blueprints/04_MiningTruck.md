# 04 · AQuarryMiningTruck —— 矿卡

**文件**：`Source/QuarryWorks/Public+Private/Vehicles/QuarryMiningTruck.{h,cpp}`
**蓝图类型**：`UCLASS(Blueprintable)`

## 职责

矿用自卸卡车：装载（挖斗卸入车斗）→ 运输 → 倾卸（BedPivot 翻转动画）→ 交付计数。

## 组件结构

```
AQuarryMiningTruck (继承 AQuarryVehiclePawn)
├─ BedPivot               车斗倾卸轴（X 轴翻转动画，最大 ~35°）
│   ├─ CargoFill          货量视觉（方块料堆随 cargo 缩放）
│   ├─ BedIntake          挖斗卸料入口（挖斗对准此处）
│   └─ DumpPoint          倾卸卸料点（对准仓库位置判定）
├─ RealVehicleMesh        真实自卸卡车身 + 6 轮（顶点聚类实测轮位）
└─ Cargo (QuarryCargoComponent)  货量状态（当前/容量/类型）
```

## 倾卸流程

```
Ctrl+2  possessed → X 键 ToggleUnloading
  前置：静止 + 货量 > 0 + 卸料点在仓库 UnloadRadius 内
  动画：BedAngle 0 → -35°（FInterpTo 缓动）
  料流：每帧 UnloadRate·dt 卸入 DeliveryPile → DeliveredMassKg 累计
  结束：货清空自动停 + 恢复状态文本
```

## 关键参数（pbtxt 可覆盖）

| 参数 | 默认 | 说明 |
|---|---|---|
| cargo_capacity_kg | 60000 | 车斗容量 |
| unload_radius | 500 | 卸料点距仓库判定半径 |
| unload work_rate | 6000 kg/s | 卸料速率 |

## 轮位实测数据（CARLA 重绑直接可用）

前轴 +308 / 后双联 −96.4 / −180.6（cm），轮距 ±92.5，轮半径 47。见 `Docs/VEHICLE_ORIENTATION.md`。

## 在编辑器中创建

1. Blueprint Class → 父类 `AQuarryMiningTruck` → `BP_MiningTruck`
2. 可调：`UnloadRadius`、Cargo 容量、`StaticBodyWheelMounts`（轮位）
