# 将 QuarryWorks 车辆导入 CARLA 1.0 —— 完整实现指南

## 前置事实

- 本地 `F:\数据集\CARLA_1.0` 是 **Shipping 打包发行版**（cooked Paks + CarlaUnreal.exe，无 .uproject/编辑器/源码）——**不能在包内导入车辆**，也不能加载自定义 uasset
- 车辆导入必须在 **CARLA 源码工程**（github.com/carla-simulator/carla，UE5 分支）中进行，用源码配套的 UE 编辑器打开 `CarlaUnreal/CarlaUnreal.uproject`
- CARLA 1.0 基于 UE5.5 fork；本项目资产为 UE5.7 序列化 → **uasset 不能直接复制**，必须走 FBX 重导

## 本项目已产出的素材（Docs/CARLA_Export 需先补 FBX）

| 资产 | 网格 | 实测数据 |
|---|---|---|
| 矿卡 | `SM_AmericanDumpTruck`（650×198×212 cm fitted） | 前轴 +308 / 后双联 −96.4、−180.6，轮距 ±92.5，轮半径 47 |
| 挖掘机（履带） | `SM_ExcavatorTracks`（263×650×384 fitted） | 履带贴地、整机 650 长 |
| 挖掘机（轮式） | `SM_ExcavatorWheels` | 同框架 4 轮变体 |

## FBX 导出（在你的 工程车辆1 或本项目编辑器 UI 中，2 分钟）

1. 用 UE 编辑器打开含网格的工程
2. Content Browser 选中 `SM_AmericanDumpTruck` → 右键 → Asset Actions → Export（选 FBX）
3. 对 `SM_ExcavatorTracks` 重复
4. 存入本目录 `Docs/CARLA_Export/`

（命令行 Python 导出在 5.8 上有 API 缺口：`ExporterFBX` 不支持 StaticMesh、Interchange `export_asset` 对此类资产返回 False——实测结论，UI 导出无此问题。）

## CARLA 源码侧实现（5 步）

### 0) 获取源码

```
git clone -b ue5-dev https://github.com/carla-simulator/carla.git
# 按 CARLA 文档构建（Setup.bat → CarlarEditor.exe），CARLA 1.0 对应 ue5-dev 分支
```

### 1) 导入网格

FBX 放入 `CarlaUnreal/Content/Carla/Static/Vehicles/Quarry/`，导入为 StaticMesh；
如需在 CARLA 内做物理车辆，则重绑为 SkeletalMesh：

- 车辆必须绑定 CARLA 基础骨架（General4WheeledVehicleSkeleton），**骨骼名/层级不可改不可增**
- PhysicsAsset：碰撞盒按车身分块（底盘/车斗/驾驶室）

### 2) Chaos Vehicle 蓝图

1. 新建 Blueprint → 父类 `BaseVehiclePawn`（Carla 插件类）
2. 添加 `ChaosWheeledVehicleMovementComponent`
3. `vehicle_wheel_setups` 数组填 4 个 WheelBlueprint + 对应骨骼名——**直接用本项目实测数据**：

| Wheel | 骨骼位置 (cm, 车辆空间) | 半径 |
|---|---|---|
| FrontLeft | (308, −92.5, −36) | 47 |
| FrontRight | (308, +92.5, −36) | 47 |
| RearLeft1 | (−96.4, −92.5, −36) | 47 |
| RearRight1 | (−96.4, +92.5, −36) | 47 |
| RearLeft2 | (−180.6, −92.5, −36) | 47 |
| RearRight2 | (−180.6, +92.5, −36) | 47 |

（6 轮车：CARLA Chaos 蓝图支持每轴 2 轮；后双联轴两个 Rear setup 共用后轴骨骼偏移）

### 3) 注册到 Actor Factory

`Unreal/Plugins/Carla/Source/Carla/Actor/VehicleFactory.cpp`：
在 `GenerateDefinitions` 的车辆列表中加入：
```cpp
VehicleDefinition({ "vehicle.quarry.dumptruck", BP_QuarryDumpTruckClass, "Quarry Dump Truck", ... });
```
并加对应的 `CarlaWheeledVehicle` 描述。

### 4) 验证

```
cd PythonAPI/examples && python spawn_npc.py  # 或：
python -c "import carla; ... world.spawn_actor(bp, spawn_point)"
```

## 备注

- 挖掘机若只作为静态观赏资产（不做 CARLA 物理），StaticMesh + MeshDescription 即可，无需骨架
- 动臂/小臂的动臂铰接数据（本项目实测 J1=(0,0,80) J2=(411,0,56) J3=(423,0,-175)）可用于 CARLA 内制作 SM_ExcavatorTracks 的可动版本（分件 + Chaos 约束），工作量较大，按需推进
