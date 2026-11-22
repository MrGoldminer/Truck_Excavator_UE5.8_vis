# 05 · UQuarryVehicleMeshComponent —— 真实资产绑定

**文件**：`Source/QuarryWorks/Public+Private/Vehicles/QuarryVehicleMeshComponent.{h,cpp}`
**蓝图类型**：`BlueprintSpawnableComponent`

## 职责

把真实静态网格资产绑定到物理车上，按 BodyMeshPath 的加载结果三路分发：

```
路径 A（骨骼网格）：SportsCar 式 —— 轮骨位置重建轮位 + 隐藏烘焙轮骨
路径 B（静态网格）：DumpTruck/整机式 —— 显式轮位挂 SM_Wheel_01
路径 C（分件装配）：PartBindings —— 部件挂关节链（挖掘机铰接臂）
```

## 路径 B：静态网格（矿卡当前用法）

1. `BodyStaticMeshPath` 加载 SM_AmericanDumpTruck
2. 自动适配：水平最大延展 → `BodyFitLength`（建模轴向不定，取 max(X,Y)）
3. 附加到底盘根：`BodyYawOffsetDeg`（−90：模型 +Y → 底盘 +X）+ `BodyOffsetX`（居中）+ `BodyZOffset`（原点贴地）
4. `StaticBodyWheelMounts` 逐个挂 SM_Wheel_01（scale 1.57）：**Mount = 悬架顶**，轮心由 `UpdateWheelVisual` 按悬架长度推导

## 路径 C：分件装配（挖掘机当前用法）

1. `PartBindings` 数组：`{PartName, ParentPivot(0=底盘/1=动臂/2=小臂/3=铲斗), RelativeLocation, RelativeRotation, bSpinWithWheels}`
2. 关节链：`BoomJointLocal` / `StickJointOffset` / `BucketJointOffset`（实测铰点模型坐标）
3. **铰点旋转由 `DriveArticulatedParts(BoomAngle, StickAngle, BucketAngle)` 驱动**——部件相对位置继承关节旋转，铰点自动钉住
4. 轮分件（枢轴居中）：`TickComponent` 里逐轮悬架跟随 + 绕轴自转

## 材质回退

分件导入常丢失材质引用：null 材质槽自动套 `M_QuarryBlockout` MID 并按部件着色（车身黄 / 轮深色）。

## 调参速查（细节面板全部可改）

| 参数 | 作用 | 当前值（矿卡） |
|---|---|---|
| BodyYawOffsetDeg | 建模轴对齐 | −90（包资产长度=Y） |
| BodyFitLength | 车长自动适配 | 650 |
| BodyOffsetX / BodyZOffset | 车身居中/离地 | −35.3 / −83 |
| StaticBodyWheelMounts | 6 轮悬架顶位 | (±308/−96.4/−180.6, ±92.5, +38.75) |
| StaticBodyWheelRadius | 物理轮半径 | 47 |
| StaticBodyWheelMeshScale | 轮网格缩放 | 1.57 |
| PartsSpinSign | 轮自转方向 | −1（前进正转） |

