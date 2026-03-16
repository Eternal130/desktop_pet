# 通信协议

> **实现状态**：Phase 1（WebSocket 通信层）已完成。协议双端均已实现并通过互操作测试验证。
>
> 本文档定义控制面板与渲染引擎之间的 WebSocket 通信协议，作为双端开发的接口契约。
> 整体架构参见 [架构总览](../README.md)，实现库选型参见 [第三方库选型](../engineering/dependencies.md)。
>
> **子文档**：[Commands](./commands.md) | [Events](./events.md) | [握手流程](./handshake.md) | [错误码](./error-codes.md) | [实现参考](./implementation.md)

---

## 一、连接参数

| 参数 | 值 | 定义位置 |
|:---|:---|:---|
| 协议 | `ws://`（本地通信，未启用 TLS） | `WebSocketClient.cpp`：`USE_TLS=OFF` |
| 地址 | `ws://localhost:9000` | — |
| 端口 | **9000** | `LAppDefine.hpp`：`DefaultWebSocketPort = 9000` |
| Server 端 | 控制面板（Java-WebSocket） | `PetWebSocketServer.java` |
| Client 端 | 渲染引擎（IXWebSocket） | `WebSocketClient.cpp` |
| Ping 间隔 | 45 秒 | `WebSocketClient.cpp`：`setPingInterval(45)` |
| 最大重连间隔 | 30 秒 | `WebSocketClient.cpp`：`setMaxWaitBetweenReconnectionRetries(30000)` |
| 自动重连 | 启用（IXWebSocket 内置） | `WebSocketClient.cpp`：`enableAutomaticReconnection()` |
| 消息队列上限 | 1000 条 | `WebSocketClient.hpp`：`kMaxQueueSize = 1000` |
| 每帧处理上限 | 50 条 | 主循环中 `drainMessages()` 后按批次处理 |
| 连接数 | 单连接（新连接替换旧连接） | `PetWebSocketServer.java`：`activeConnection` 管理 |
| 序列化 | JSON 文本帧 | C++: nlohmann/json, Java: Gson |

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
| `id` | string | ✓ | 消息唯一标识。C++ 端使用 `mt19937_64` 生成数字字符串，Java 端使用 `UUID.randomUUID()` |
| `payload` | object | ✓ | 消息负载数据。无负载时为空对象 `{}` |
| `timestamp` | number (int64) | ✓ | 发送时间戳（Unix 毫秒） |

**反序列化规则**：缺少任一必填字段时，`deserialize()` 返回空（C++ 返回 `std::nullopt`，Java 返回 `Optional.empty()`）。JSON 格式错误或空字符串同样返回空，不抛异常。

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

**Response 的 `id` 字段**：复用原始 Command 的 `id`，用于 request-response 匹配。Java 端通过 `MessageDispatcher.expectResponse(id, timeout)` 返回 `CompletableFuture<Envelope>`，按 `id` 完成匹配。

**序列化规则**：`serialize()` 方法仅在 `type == "response"` 时写入 `success`/`error_code`/`error_message` 字段。非 Response 消息的 JSON 中不包含这三个字段。

### 2.3 消息方向

| type | 方向 | 发送方 | 接收方 |
|:---|:---|:---|:---|
| `command` | 控制面板 → 渲染器 | Java（Server） | C++（Client） |
| `event` | 渲染器 → 控制面板 | C++（Client） | Java（Server） |
| `response` | 渲染器 → 控制面板 | C++（Client） | Java（Server） |

### 2.4 双向消息

| action | 说明 |
|:---|:---|
| `ping` / `pong` | 心跳保活（IXWebSocket 内置，间隔 45 秒） |
