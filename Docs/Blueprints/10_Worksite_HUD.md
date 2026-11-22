# 10 · 场景装配 + HUD + 配置

## AQuarryWorksite —— 场景装配器

**文件**：`Worksite/QuarryWorksite.{h,cpp}`

`InitializeWorksite` 生成全部 Actor：挖掘机/矿卡/料场/卸料区 + 注册到子系统 + 地形标记。

## UQuarryWorksiteSubsystem —— 编排中枢（TickableWorldSubsystem）

每帧顺序：越界复位 → TCP 命令出队 → MQTT 入站路由（命令/任务/截图）→ 遥测发布 → 任务执行 → 同步快照 → TCP 广播。

另含：越界复位（车掉出地图自动回出生点）、MQTT 在线状态、CARLA 式同步模式（SetSynchronousMode）。

## AQuarryHUD —— HUD

状态面板（速度/挡位/转速/轮况打滑/货量/交付/TCP 状态）+ 操作提示 + 世界标记。

## UQuarryConfigSubsystem —— 配置

启动时读 `Config/QuarryWorks.pbtxt`（文本格式 protobuf）覆盖车辆默认参数；schema_version=2 含力驱动模型覆盖项。
