# 09 · FQuarryTaskExecutor —— 任务执行器 + 测试套件

**文件**：`Source/QuarryWorks/Public+Private/Worksite/QuarryTaskExecutor.{h,cpp}`

## 任务类型

| 类型 | 参数 | 行为 |
|---|---|---|
| `drive_to` | vehicle/x/y/tolerance | 航向 P 律导航到目标（宽弧线+倒车入库） |
| `dig` | — | 自主挖掘循环（回转→探料→扫掠→装满） |
| `haul` | — | 车斗载货→驶向仓库→倾卸 |
| `test_suite` | — | 顺序执行 6 用例，逐个发布结果+汇总 |

## 导航控制器（DriveVehicleToward）

```
容差内 → 制动停稳 → Completed
|err| > 90° → 尾对目标倒车（自卸车卸料不看车头朝向）
|err| ≤ 90° → 航向 P 律（转向限幅 0.85 防 6m 小圈饱和；油门不压降）
履带式 |err| > 15° → 原地回转
```

## 测试套件 6 用例

1. `spawn_stability` 驻车 5s 位移 < 8cm
2. `wheel_contact` 全轮接地
3. `drive_brake` 30m 加速→刹停 < 20m
4. `suspension_settle` Z 振幅 < 1.5cm
5. `sand_slip` 上沙堆打滑
6. `auto_haul_cycle` 完整挖运卸（交付 ≥ 500kg）

阈值全部在 `Config/DefaultGame.ini [QuarryWorks]` 可调。
