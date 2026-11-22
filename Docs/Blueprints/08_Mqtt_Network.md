# 08 · MQTT 管理面 + TCP 桥 + JSON 编解码

## FQuarryMqttClient —— 自研 MQTT 3.1.1 客户端

**文件**：`Source/QuarryWorks/Public+Private/Protocol/QuarryMqttClient.{h,cpp}`

- **协议实现**：CONNECT/CONNACK/SUBSCRIBE/SUBACK/PUBLISH(QoS0/1)/PUBACK/PINGREQ/PINGRESP/DISCONNECT 全手工编解码（varint 剩余长度、UTF-8 字符串打包）
- **线程模型**：独立 FRunnable 工作线程；出站 = 游戏线程推送 → 有界队列（128，丢最旧）→ 工作线程逐包发送；入站 = 工作线程收包解析 → MPSC 队列 → 游戏线程路由
- **保活策略**（纯发布型会话特有）：外发遥测刷新 LastSentTime；入站安静只触发 PINGREQ 探测；探包无响应才断线
- **重连**：指数退避（2→10s）；CONNACK 拒绝时轮换 ClientId
- **安全性**：socket 由工作线程独占管理；RequestStop 仅 join 后清指针（已修 exit 竞态崩溃）

## 主题表

| 主题 | 方向 | QoS | 内容 |
|---|---|---|---|
| `quarryworks/v1/telemetry` | →管理端 | 0 | 20Hz CARLA JSON |
| `quarryworks/v1/command` | 管理端→ | 1 | VehicleControl |
| `quarryworks/v1/task` | 管理端→ | 1 | 任务下发 |
| `quarryworks/v1/task/result` | →管理端 | 0 | 任务结果 |
| `quarryworks/v1/test/result` | →管理端 | 0 | 测试用例+汇总 |
| `quarryworks/v1/status` | →管理端 | 0 | retained online/offline |

## FQuarryJsonCodec —— CARLA 命名 JSON 编解码

**文件**：`Protocol/QuarryJsonCodec.{h,cpp}`

- `EncodeTelemetry`：location/velocity（米）、imu（m/s²·rad/s·compass）、gnss（度）、wheels[]（slip/load）、cargo、rpm/gear
- `ParseControl`：CARLA VehicleControl 语义 → 带符号油门
- `ParseTask`：任务 JSON → FQuarryTaskCommand

## FQuarryTcpServer —— protobuf TCP 桥

**文件**：`Protocol/QuarryTcpServer.{h,cpp}`

- 帧协议 `[u32 LE 长度][protobuf]`；单客户端；游戏线程发、工作线程收
- 与 MQTT 共用 `ApplyVehicleCommand` 入口

## 已修复的历史坑（勿回归）

1. 纯发布型会话的 keepalive 误判 → PINGREQ 探测制
2. payload 无终止符 → UTF8 读越界垃圾（'UUU'）→ 补 '\0'
3. CONNACK 拒绝后 Socket 悬空 → break guard
4. socket 所有权竞态 → 工作线程独占
