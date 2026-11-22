# 06 · 可变形地形 + 料流系统

## AQuarryDigSite —— 可变形沙堆料场

**文件**：`Source/QuarryWorks/Public+Private/Terrain/QuarryDigSite.{h,cpp}`

- `TerrainMesh`（ProceduralMeshComponent）：33×33 顶点高度场，中心隆起 MoundHeight
- **挖掘同步形变**：`ExtractAt` 扣料 → `ApplyHeightDelta` 体积守恒下陷（权重 = 1−d²/r²）
- **堆料隆起**：卸料区的 DepositAt 反向堆积
- 碰撞：BlockAll + 自定义 `QuarryCollision::Terrain` 通道（车轮 raycast 命中它 → 低摩擦）
- **性能**：全量重建 O(1089)，异步烹饪；优化方向 = 3×3 分区局部更新

## AQuarryMaterialPile —— 料堆基类

**文件**：`Source/QuarryWorks/Public+Private/Materials/QuarryMaterialPile.{h,cpp}`

- `Extract/ExtractAt`：挖取（有限源按剩余给量，挖空即止）
- `Deposit/DepositAt`：堆放（同类型才收，容量可选）
- `OnPileChanged` 委托：料变事件（交付计数/视觉缩放的来源）
- `MaterialHardness`：挖掘效率的硬度因子（沙 1.0 / 砾石 0.7 / 矿 0.45）

## UQuarryCargoComponent —— 货斗

**文件**：`Source/QuarryWorks/Public+Private/Cargo/QuarryCargoComponent.{h,cpp}`

- `Load`：装载（同类型混装约束）
- `Unload`：卸载
- `TransferTo`：挖斗→车斗的守恒转移（目标容量预留 + 差额回滚）
- `OnCargoChanged` 委托 → 车斗视觉/悬架质心/遥测同步

## 料流全链路

```
挖斗挖掘: DigSite.ExtractAt(tip, rate·dt·efficiency)
    ↓  BucketCargo.Load(extracted)
装车: BucketCargo.TransferTo(TruckCargo, ...)   ← 料流守恒
运输: TruckCargo 随车移动
卸货: DeliveryPile.DepositAt(DumpPoint, amount)
    ↓  DeliveredMassKg += ...（交付指标）
```
