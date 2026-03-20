# 断连缓存与实现参考

> 协议概述参见 [通信协议](./README.md)。
> 握手与连接流程参见 [握手流程](./handshake.md)。
>
> 本文档分为两部分：§一为语言无关的断连缓存策略（协议层），§二/§三为当前 C++/Java 参考实现的关键接口。

---

## 一、断连缓存策略

控制面板在渲染器断连期间，对外发指令应执行分类处理：

| 指令 action | 断连时行为 | 理由 |
|:---|:---|:---|
| `load_model` | ✅ 缓存 | 重连后必须恢复模型 |
| `set_position` | ✅ 缓存 | 重连后恢复窗口位置 |
| `set_opacity` | ✅ 缓存 | 重连后恢复透明度 |
| `play_motion` | ❌ 丢弃 | 过时的动作指令无意义 |
| `set_expression` | ❌ 丢弃 | 非关键，重连后由业务逻辑重设 |
| `stop_motion` | ❌ 丢弃 | 非关键 |
| `set_scale` | ❌ 丢弃 | 未完整实现，非关键 |
| `hello` | ❌ 丢弃 | 当前流程未使用，非关键 |
| `shutdown` | ❌ 丢弃 | 断连时渲染器已不可达 |

缓存指令存入线程安全队列，重连后在 `ready` 事件处理中按入队顺序重放。

---

## 二、C++ 端关键接口

| 类/函数 | 文件 | 说明 |
|:---|:---|:---|
| `Network::Envelope` | `Protocol.hpp` | 消息结构体（type, action, id, payload, timestamp + response 字段） |
| `Network::serialize()` | `Protocol.cpp` | Envelope → JSON string |
| `Network::deserialize()` | `Protocol.cpp` | JSON string → `optional<Envelope>`。缺少必填字段或 JSON 错误返回 `nullopt` |
| `Network::createCommand()` | `Protocol.cpp` | 工厂方法，自动填充 type="command"、id（随机数字）、timestamp |
| `Network::createEvent()` | `Protocol.cpp` | 工厂方法，type="event" |
| `Network::createResponse()` | `Protocol.cpp` | 工厂方法，type="response"，`id` 复用原始 Command 的 id，payload 固定为 `{}` |
| `Network::generateId()` | `Protocol.cpp` | `mt19937_64` + `uniform_int_distribution<uint64_t>` 生成数字字符串 |
| `Network::MessageHandler` | `MessageHandler.hpp` | 按 action 注册 `CommandHandler`，dispatch 时过滤 response（返回 nullopt），未知 action 返回 Response（error_code 5003） |
| `Network::EventEmitter` | `EventEmitter.hpp` | 封装 `createEvent()` + `serialize()` + 发送回调 |
| `Network::WebSocketClient` | `WebSocketClient.hpp` | IXWebSocket 封装，线程安全消息队列（上限 1000 条），`drainMessages(maxCount)` 批量取出（默认全部，渲染主循环传入 50 实现每帧上限），45 秒 Ping 间隔，断连自动重连（指数退避，上限 30 秒） |
| `Network::RegisterCommandHandlers()` | `CommandHandlers.cpp` | 注册所有 command handler（load_model, play_motion, stop_motion, set_expression, set_position, set_scale, set_opacity, hello, shutdown） |

---

## 三、Java 端关键接口

| 类/方法 | 文件 | 说明 |
|:---|:---|:---|
| `Envelope` | `model/Envelope.java` | Java Record。response 字段（success, errorCode, errorMessage）为 `null` 表示非 response |
| `Protocol.serialize()` | `network/Protocol.java` | 手动构建 `JsonObject`，response 字段在顶层 |
| `Protocol.deserialize()` | `network/Protocol.java` | 返回 `Optional<Envelope>`。非 response 消息的 success/errorCode/errorMessage 为 null |
| `Protocol.createCommand()` | `network/Protocol.java` | type="command"，id 使用 `UUID.randomUUID()` |
| `MessageDispatcher` | `network/MessageDispatcher.java` | response → `CompletableFuture` 匹配 id；event → `Consumer<Envelope>` 按 action 路由 |
| `MessageDispatcher.expectResponse()` | 同上 | 注册 `CompletableFuture<Envelope>`，支持 `Duration` 超时，超时后自动清理 |
| `PetWebSocketServer` | `network/PetWebSocketServer.java` | 单连接管理，`sendMessage()` / `setMessageCallback()` / `setConnectionCallback()` |
| `AppOrchestrator` | `core/AppOrchestrator.java` | 生命周期编排，事件处理器注册，`sendOrCache()` 断连策略 |
