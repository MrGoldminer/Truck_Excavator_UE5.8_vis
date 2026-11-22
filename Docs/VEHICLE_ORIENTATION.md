# 车辆轴向与轮位约定（实测数据）

本文档一次性定义全项目车辆的轴向约定与每个第三方网格的实测安装参数。**所有数值来自网格顶点/边界测量，不是手工试凑**。

## 底盘坐标系（约定）

- **前进 = +X**，**右侧 = +Y**，**上方 = +Z**（UE 左手系）
- **轮轴方向 = 底盘 Y（横向）**；轮子滚动 = 绕轮轴的 pitch 旋转
- **滚动方向**：前进（+X）时轮子绕 **−Y** 旋转（右手定则，接地点在下方：ω = v/r 取负）——所有轮子可视化统一取负号
- 顶部方块（挖掘机上部结构）绕 Z 轴回转，不影响底盘约定

## 网格实测表（顶点边界 + 底部聚类）

| 网格 | 原始尺寸 (cm) | 长度轴 | 底部 Z | 安装参数 |
|---|---|---|---|---|
| `SM_Wheel_01`（卡车轮） | 60 × **17(轴)** × 60 | 轴向已 = Y ✓ | — | 基座旋转 (0,0,0)，滚向取负 |
| `SM_ExcavatorTracks` | 299 × **741** × 437 | **Y**（不对称 −200..+541，中心 +170.7） | Z≈0（原点即地面） | yaw −90，offset (−103.6, 0, −149)，scale 0.607（fit 450） |
| `SM_ExcavatorWheels` | 292 × **767** × 436 | Y（同上框架） | Z≈0 | 同履带安装 |
| `SM_AmericanDumpTruck` | 295 × **965** × 695 | **Y**（−430..+535，中心 +52.4） | Z≈0 | yaw **−90**，offset X −35.3、Z −83，scale 0.674（fit 650） |

**通用规律**：该资产包（ConstructionMachines）所有车辆网格都按"长度 = 模型 Y、原点在地面"建模；组装时 yaw −90 使模型 +Y → 底盘 +X。

## 矿卡轮位（SM_AmericanDumpTruck 顶点聚类实测）

模型空间（前轴 Y=+457，后双联轴 Y=−143 / −268，轮距 X=±137.5）→ 底盘空间（yaw −90 + scale 0.674）：

| 轮 | 底盘坐标 (X, Y, Z) |
|---|---|
| 前轴 左/右 | (+308, ±92.5, −36) |
| 后 1 左/右 | (−96.4, ±92.5, −36) |
| 后 2 左/右 | (−180.6, ±92.5, −36) |

- 轮半径（缩放后）≈ 47cm；SM_Wheel_01 原生半径 30 → 轮网格缩放 1.57
- 卡车身长 650、宽 198、高 212（缩放后），底盘根距地 ~83cm

## 代码映射

- `UQuarryVehicleMeshComponent`：`WheelBaseRotation` / `BodyYawOffsetDeg` / `BodyOffsetX` / `BodyZOffset` / `StaticBodyWheelMounts` / `StaticBodyWheelRadius` / `StaticBodyWheelMeshScale` / `TracksYawOffsetDeg` / `TracksOffset` / `TrackFitLength`
- `UQuarryVehicleMovementComponent::UpdateWheelVisual`：滚动取负（−ω·dt）
- `UQuarryVehicleMeshComponent::TickComponent`：分件装配轮旋转 `PartsSpinSign = −1`

## 测量方法（复现）

`/tmp/mesh_analysis/` 下脚本经 `UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=...` 无头执行：边界 + 三轴顶点直方图 + 底部顶点 25cm 网格聚类（卡车轮位）。顶点 API：`mesh.get_static_mesh_description(0)` + `unreal.VertexID`（注意大小写）。
