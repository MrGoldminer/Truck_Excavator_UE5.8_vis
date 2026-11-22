# 01 · QuarryVehicleMovementComponent —— 力驱动物理核心

**文件**：`Source/QuarryWorks/Public+Private/Vehicles/QuarryVehicleMovementComponent.{h,cpp}`
**蓝图类型**：`BlueprintSpawnableComponent`（可在任意 Actor 的组件面板添加）

## 职责

把"轮子可视化 + 车辆物理"从直接设速度改为 **力学驱动**（CARLA/Chaos 车辆同源方法）：

```
TickWheeledDrive（轮式）:
  转向输入 → 前轮角度（车速衰减 + 速率限制）
  每轮: UpdateSuspension (raycast → 弹簧阻尼力)
  ApplyAntiRollForces (左右轮压缩差 → 反向力偶)
  UpdatePowertrain (转速反馈 → 挡位 → 轮驱动扭矩)
  每轮: UpdateTireForces (滑移率/侧偏 → Pacejka 力, 摩擦圆钳制)
  空气阻力 ∝ v²

TickTrackedDrive（履带，挖掘机旧驱动，轮挖改后保留代码）:
  左右履带速度伺服 → 差速转向 + 土壤转向阻力
```

## 关键函数逐个说明

| 函数 | 职责 | 核心公式 |
|---|---|---|
| `UpdateSuspension` | 每轮 raycast 探地 → 弹簧阻尼力施加在悬架顶 | F = k·x + c·ẋ（只能推） |
| `UpdateTireForces` | 滑移率 κ / 侧偏角 α → 轮胎力 | F = μN·sin(C·atan(B·slip))，摩擦圆联合钳制 |
| `UpdatePowertrain` | 转速反馈 → 自动换挡 → 每轮驱动扭矩 | 扭矩曲线（怠速0.55→峰值1.0→红线0.72）× 传动比 |
| `ApplyAntiRollForces` | 左右轮压缩差 → 抗侧倾力偶 | F = k_arb·(压缩差) |
| `CalibrateFromDefaults` | 首帧按质量自动标定弹簧/阻尼/风阻 | 目标压缩 45% 行程；阻尼 2ζ√(km) |
| `UpdateWheelVisual` | 轮网格：位置 = 悬架顶−长度；自转取 −ω（前进正转） | — |

## 关键约定

- **力单位**：内部用 UE 力（kg·cm/s²），100 UE = 1 N
- **悬架顶约定**：`AddWheel(Mount,...)` 的 Mount 是悬架顶部；轮心 = Mount − SuspensionLength
- **滚动方向**：前进时绕 −Y 轴旋转（右手定则），全部轮子统一取负
- **摩擦来源**：raycast 命中物 = AQuarryDigSite → 低摩擦（沙堆）；其他 → GroundFriction
- **质心**：`SetPayloadMassKg` 会移动质心 → 满载时操纵特性真实变化

## 在编辑器中创建

1. 选择任意带物理 Box 的 Actor → Add Component → **QuarryVehicleMovementComponent**
2. 关键参数（全部可在细节面板调）：
   - `SuspensionStiffness/Damping`（0 = 按质量自动标定）
   - `ForwardGearRatios`（5 挡数组）/ `FinalDriveRatio`
   - `TireFrictionScale` / `GroundFriction` / `LooseMaterialFriction`
   - `bLegacyKinematic = true` 可一键回退旧直驱
3. 代码侧：`AddWheel(MountOffset, Radius, bDriven, bSteered, Visual)` 逐轮注册

## 调试入口

- HUD 的 `TIRE SLIP` / `Wheels: N/N grounded` 直接读本组件状态
- `GetGearLabel()` / `GetEngineRPM()` / `GetMaxWheelSlipRatio()` 遥测可查
