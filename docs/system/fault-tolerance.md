# 容错与错误处理

> 系统设计概述参见 [系统设计](./README.md)，整体架构参见 [架构总览](../README.md)。
> 断连缓存策略详见 [协议 - 实现参考](../protocol/implementation.md)。

---

## 一、MVP 阶段：模型加载失败

MVP 阶段渲染引擎独立运行，模型路径硬编码。加载失败时的处理：

```plain
渲染器启动，尝试加载硬编码模型路径
     │
     ├─ 加载成功 → 进入主循环
     └─ 加载失败 → 输出错误日志，退出进程
```

---

## 二、渲染器崩溃恢复 ✅ 已实现

由 `MainWindowController` 负责渲染器进程的监控与自动重启，实现指数退避策略：

**重启参数**（定义于 `MainWindowController`）：

| 参数 | 值 | 说明 |
|:---|:---|:---|
| 最大重试次数 | **5** | 连续崩溃超过此次数后放弃重启 |
| 退避序列 | **2s → 4s → 8s → 16s → 30s** | 每次重启前的等待时间 |
| 稳定恢复阈值 | **60 秒** | 若距上次成功启动超过此时间，重置重试计数器 |
| 重试计数器重置 | 收到 `load_model` 成功回执时 | 同时更新 `lastSuccessfulStartTime` |

**崩溃恢复流程**：

```plain
ProcessManager 检测到渲染器进程退出（非零退出码）
     │
     ├─ 退出码 = 0（正常关闭）→ 不重启
     └─ 退出码 ≠ 0（崩溃）→ 触发 scheduleRestart()
          │
          ├─ 距上次成功启动 > 60s → 重置 restartAttempts = 0
          ├─ restartAttempts ≥ 5 → 放弃重启，记录错误日志
          └─ restartAttempts < 5 → 等待退避时间后执行 doRestartRenderer()
               │
               ▼
          重新启动渲染器进程
               │
               ▼
          渲染引擎连接 WebSocket Server → 发送 ready 事件
               │
               ▼
           MainWindowController 收到 ready → 发送 load_model + set_position
               │
               ▼
          load_model 成功回执 → 重置 restartAttempts = 0, 更新 lastSuccessfulStartTime
               │
               ▼
          恢复完成，刷新缓存的关键指令，恢复 Scheduler
```

**关键**：控制面板始终维护当前模型路径和窗口位置的内存副本（`PetStateManager` + `ConfigManager`），不依赖渲染器侧的状态。

---

## 三、WebSocket 断连处理 ✅ 已实现

**渲染引擎（Client）断连处理**：

| 阶段 | 行为 |
|:---|:---|
| 检测 | IXWebSocket 内置心跳检测 |
| 重连策略 | IXWebSocket 内置自动重连（指数退避） |
| 重连成功 | 重新发送 `ready` 事件，由控制面板重发状态同步指令 |

**控制面板（Server）断连处理**（`MainWindowController` 实现）：

| 阶段 | 行为 |
|:---|:---|
| 检测 | `PetWebSocketServer` 的 `connectionCallback` 通知断开 |
| 即时操作 | Scheduler `pause()`（停止闲时动作触发） |
| 进程判定 | 结合 `ProcessManager.isRunning()` 判断渲染器是否存活 |
| 进程存活 | 等待渲染引擎重新连接（网络抖动场景） |
| 进程退出 | 判定为渲染引擎崩溃，触发自动重启流程（见第二节） |

**断连期间指令缓存策略**（`sendOrCache` 方法，完整策略详见 [协议 - 实现参考](../protocol/implementation.md)）：

| 指令类型 | 断连时行为 | 原因 |
|:---|:---|:---|
| `load_model` | ✅ 缓存到 `ConcurrentLinkedQueue` | 关键指令，重连后必须恢复模型 |
| `set_position` | ✅ 缓存 | 关键指令，重连后恢复窗口位置 |
| `set_opacity` | ✅ 缓存 | 关键指令，重连后恢复透明度 |
| `set_size` | ✅ 缓存 | 关键指令，重连后恢复窗口尺寸 |
| `set_fps` | ✅ 缓存 | 关键指令，重连后恢复帧率设置 |
| `set_hit_areas` | ✅ 缓存 | 关键指令，重连后恢复命中区域配置 |
| `set_layout` | ✅ 缓存 | 关键指令，重连后恢复用户布局 |
| `set_volume` | ✅ 缓存 | 关键指令，重连后恢复音量/静音状态 |
| `play_motion` | ❌ 丢弃（DEBUG 日志） | 非关键，过时的动作指令无意义 |
| `play_motion_ext` | ❌ 丢弃 | 过时的外部动作指令无意义 |
| `set_expression` | ❌ 丢弃 | 非关键 |
| `stop_motion` | ❌ 丢弃 | 非关键 |
| `play_audio` | ❌ 丢弃 | 过时的音频播放无意义 |
| `stop_audio` | ❌ 丢弃 | 非关键 |
| `get_layout` | ❌ 丢弃 | 查询指令，响应已过期 |
| `reset_layout` | ❌ 丢弃 | 一次性操作，重连后由 `set_layout` 恢复 |
| `get_stats` | ❌ 丢弃 | 轮询指令，重连后重新发起 |

**重连后恢复流程**：收到 `ready` 事件后，先执行 `load_model` + `set_position`，再调用 `flushPendingCommands()` 按缓存顺序重放所有关键指令，最后 `Scheduler.resume()` 恢复闲时动作触发。

---

## 四、Phase 2：模型加载失败（控制面板协作）

> **Phase 2 实现**：引入控制面板后，加载失败支持用户通知和自动回退。

```plain
控制面板发送 load_model
     │
     ▼
渲染器上报 model_load_failed（含 error_code）
     │
     ▼
控制面板处理：
  1. 弹窗通知用户加载失败原因
  2. 自动尝试加载上一个成功的模型
     │
     ├─ 上一个模型加载成功 → 恢复正常
     └─ 上一个模型也失败 → 保持无模型状态，通知用户
```
