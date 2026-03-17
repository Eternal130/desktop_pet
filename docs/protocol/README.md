# 通信协议

> **实现状态**：Phase 1（WebSocket 通信层）已完成。协议双端均已实现并通过互操作测试验证。
>
> 本文档定义控制面板与渲染引擎之间的 WebSocket 通信协议，作为双端开发的接口契约。
> 整体架构参见 [架构总览](../README.md)，实现库选型参见 [第三方库选型](../engineering/dependencies.md)。
>
> **子文档**：[Commands](./commands.md) | [Events](./events.md) | [握手流程](./handshake.md) | [错误码](./error-codes.md) | [实现参考](./implementation.md)

---

## 一、连接参数

| 参数 | 值 | 说明 |
|:---|:---|:---|
| 协议 | `ws://` | 本地通信，未启用 TLS |
| 地址 | `ws://localhost:9000` | — |
| 端口 | **9000** | — |
| Server 端 | 控制面板 | WebSocket Server，监听端口等待渲染引擎连接 |
| Client 端 | 渲染引擎 | WebSocket Client，启动后主动连接控制面板 |
| 帧类型 | Text Frame | 仅使用 WebSocket 文本帧，不使用 Binary Frame |
| 编码 | UTF-8 | WebSocket Text Frame 默认编码 |
| 序列化 | JSON | 所有消息为合法 JSON（RFC 8259） |
| Ping 间隔 | 45 秒 | 渲染引擎发起 WebSocket 协议层 Ping（RFC 6455），多数 WebSocket 库自动回复 Pong，无需应用层处理 |
| 最大重连间隔 | 30 秒 | 渲染引擎断连后指数退避重连，上限 30 秒 |
| 自动重连 | 启用 | 渲染引擎断连后自动重连，控制面板无需干预 |
| 消息队列上限 | 1000 条 | 渲染引擎侧接收缓冲区上限 |
| 每帧处理上限 | 50 条 | 渲染引擎每渲染帧最多处理 50 条入站消息 |
| 连接数 | 单连接 | 控制面板同一时刻仅维护一个活跃连接，新连接建立时关闭旧连接（Close Code 1000） |

> 当前参考实现的源码位置见 [实现参考](./implementation.md)。

---

## 二、消息 Envelope 格式

所有 WebSocket 消息均使用统一的 JSON Envelope 格式。

### 2.1 基础 Envelope

```json
{
  "type": "command",
  "action": "play_motion",
  "id": "12345678901234567",
  "payload": { "group": "Idle", "index": 0, "priority": 1 },
  "timestamp": 1710000000000
}
```

| 字段 | JSON 类型 | 必填 | 说明 |
|:---|:---|:---:|:---|
| `type` | string | ✓ | 消息类型。可选值：`"command"` \| `"event"` \| `"response"` |
| `action` | string | ✓ | 操作名称（如 `"load_model"`、`"hit"`） |
| `id` | string | ✓ | 消息唯一标识，用于 request-response 匹配。任意非空字符串，建议不超过 64 字符。渲染引擎生成数字字符串，控制面板可使用任意格式（如 UUID） |
| `payload` | object | ✓ | 消息负载数据。无负载时为空对象 `{}` |
| `timestamp` | number (int64) | ✓ | 发送时间戳（Unix 毫秒） |

**反序列化规则**：缺少任一必填字段时，消息视为无效并丢弃。JSON 格式错误或空字符串同样视为无效。实现应静默丢弃无效消息，不抛异常。

### 2.2 Response Envelope

> ⚠️ **关键约定**：Response 的 `success`、`error_code`、`error_message` 字段位于 **JSON 顶层**，与 `type`、`action` 等字段平级，**不在 `payload` 内**。`payload` 在 Response 中固定为空对象 `{}`。此为双端共同约定。

```json
{
  "type": "response",
  "action": "load_model",
  "id": "12345678901234567",
  "payload": {},
  "timestamp": 1710000000100,
  "success": true,
  "error_code": 0,
  "error_message": ""
}
```

| 字段 | JSON 类型 | 仅 Response | 说明 |
|:---|:---|:---:|:---|
| `success` | boolean | ✓ | 操作是否成功 |
| `error_code` | number (int) | ✓ | 错误码。成功时为 `0`，失败时为具体错误码（见 [错误码](./error-codes.md)） |
| `error_message` | string | ✓ | 错误描述。成功时为空字符串 `""` |

**Response 的 `id` 字段**：复用原始 Command 的 `id`，用于 request-response 匹配。控制面板应维护 pending request 表，收到 Response 时按 `id` 查找并完成匹配。建议设置 **10 秒超时**，超时后清理 pending 记录。

**序列化规则**：仅当 `type == "response"` 时写入 `success`/`error_code`/`error_message` 字段。非 Response 消息的 JSON 中**不包含**这三个字段。控制面板反序列化时，对非 Response 消息应将这三个字段视为不存在（置为空/null）。

### 2.3 消息方向

| type | 方向 | 发送方 | 接收方 |
|:---|:---|:---|:---|
| `command` | 控制面板 → 渲染器 | 控制面板（Server） | 渲染引擎（Client） |
| `event` | 渲染器 → 控制面板 | 渲染引擎（Client） | 控制面板（Server） |
| `response` | 渲染器 → 控制面板 | 渲染引擎（Client） | 控制面板（Server） |

### 2.4 心跳

渲染引擎每 45 秒发送一次 WebSocket 协议层 Ping 帧（RFC 6455 opcode `0x9`），控制面板需回复 Pong 帧（opcode `0xA`）。**这是 WebSocket 传输层行为，不是应用层 JSON 消息**，多数 WebSocket 库会自动处理 Pong 回复，无需编写应用代码。

若控制面板持续未回复 Pong，渲染引擎可能判定连接失效并触发重连。

---

## 三、消息时序规则

### 3.1 连接初始化

1. WebSocket 连接建立后，渲染引擎**主动发送 `ready` 事件**
2. 控制面板 **必须** 等待收到 `ready` 事件后，才能开始发送 command
3. `ready` 之前发送的 command，渲染引擎不保证处理

### 3.2 命令并发

- 控制面板可以**并发发送多个 command**，无需等待前一个 command 的 response
- Response 通过 `id` 字段与原始 Command 匹配，**不依赖消息到达顺序**
- Event 可能在 command 和其 response 之间到达（异步事件）
- 同一 action 的多个 command 按接收顺序依次处理，不做合并或去重

### 3.3 渲染引擎处理节奏

- 渲染引擎每渲染帧最多处理 50 条入站消息
- 超出部分在下一帧继续处理（队列缓冲，上限 1000 条）
- 控制面板短时间内大量发送 command 不会丢失，但处理会有延迟
