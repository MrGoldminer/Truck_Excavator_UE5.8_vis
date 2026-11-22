# QuarryWorks 蓝图构建指南 —— 从空场景到完整采石场

本指南描述编辑器里的完整搭建步骤。项目已全部配置好（打开即可玩），
本文用于**理解结构**或**在空工程中重建**。

## 第 1 步 · 插件与工程配置

`QuarryWorks.uproject` 已启用：
`ChaosVehiclesPlugin / ChaosModularVehicleExamples / NetworkPredictionExtras /
PythonScriptPlugin / ModelContextProtocol / AllToolsets` 等。
Build.cs 依赖：`Core / Engine / EnhancedInput / PhysicsCore / ProceduralMeshComponent /
Sockets / Networking / Json / Protobuf`。

## 第 2 步 · 关卡（Content/Levels/QuarryLevel.umap）

- 地面：大面积平面 + `QuarryCollision::Terrain` 通道（车轮 raycast 目标）
- 挖掘场：**AQuarryDigSite**（33×33 可变形高度场）放 (650,-450)
- 交付区：**AQuarryMaterialPile**（DeliveryPile，bCountsAsDelivery）放 (-650,-350)
- 挖掘场标记：**AQuarryWorksite** 总装 Actor（InitializeWorksite 生成两台车）
- 相机：每车自带 SpringArm 追踪相机（Horizon 风格滞后）

## 第 3 步 · 车辆（无需手搭 BP，C++ 构造函数全装配）

| 车辆 | C++ 类 | 真机视觉 |
|---|---|---|
| 挖掘机 | `AQuarryExcavator` | 分件装配（Body/Door/4 轮/动臂/小臂/铲斗，铰接驱动）|
| 矿卡 | `AQuarryMiningTruck` | SM_AmericanDumpTruck 整身 + 6×SM_Wheel_01（实测轮位）|

想自建 BP：Content Browser → 右键 → Blueprint Class → 选对应 C++ 类 →
命名 BP_XXX → 双击打开调参数 → 拖入关卡。

## 第 4 步 · 输入（IMC 运行时动态构建）

无 .ini 输入资产——`SetupPlayerInputComponent` 在代码里动态创建 IMC：
W/S 油门 A/D 转向 Space 制动 Shift 驻车 鼠标相机；
挖掘机附加 Q/E 回转 R/F 动臂 T/G 小臂 Y/H 铲斗 LMB 挖 RMB 卸；
矿卡附加 X 倾卸。

## 第 5 步 · MQTT 管理面（可选）

```
游戏控制台: Quarry.Server.Start     # TCP protobuf 桥 :17000
Quarry.Mqtt 自动随游戏启动（连 127.0.0.1:1883）
```
Python 管理端：`python Scripts/mqtt_manager_sim.py --suite`（详见 README）。

## 第 6 步 · 资产依赖（不在 git 里）

- `Content/ConstructionMachines/`：从 `F:\code\工程车辆1\Content\资源包\epic内容\工程车辆1\ConstructionMachines\` 复制（官方免费包）
- 注意：包内 uasset 的材质引用指向原路径 `/Game/资源包/...`，缺失时由
  `UQuarryVehicleMeshComponent` 的 MID 回退（块状着色）兜底

## 第 7 步 · 验证

- PIE → Ctrl+2 开矿卡 → W 前进 → 确认轮子向前滚、悬架起伏
- 挖掘机 Ctrl+1 → LMB 挖掘 → 确认铲斗对准画面位置、地形下陷
- `Quarry.Server.Start` 后跑 `python Scripts/mqtt_manager_sim.py --suite` → 期望 6/6
