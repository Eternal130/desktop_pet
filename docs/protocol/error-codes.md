# 错误码体系

> 协议概述参见 [通信协议](./README.md)。

按模块分段编码，每段预留 100 个错误码用于扩展。

---

## 一、模型相关（1000-1099）

| 错误码 | 常量 | 触发场景 | 伴随消息 |
|:---:|:---|:---|:---|
| 1001 | — | `load_model` 时 `model_path` 为空或路径不存在 | `"model_path is required"` / `"Model path not found: <path>"` |
| 1002 | — | 模型格式不兼容（保留） | — |
| 1003 | — | 纹理加载失败（保留） | — |
| 1004 | — | Cubism SDK 初始化失败（保留） | — |
| 1005 | — | `set_hit_areas` 时 `hit_areas` 缺失或非数组 | `"hit_areas array is required"` |

---

## 二、动画相关（2000-2099）

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 2001 | `play_motion` 时无模型加载 | `"No model loaded"` |
| 2002 | 动作索引越界（保留） | — |
| 2003 | 表情 ID 不存在（保留） | — |

---

## 三、外部动作相关（3000-3099）— Phase 3a（`play_motion_ext`）

> **段位复用说明**：早期文档将 3000 段规划为音频错误码。实现 Phase 3a/3b 时，3000 段被分配给外部动作指令 `play_motion_ext`，音频模块改用 [7000 段](#七音频相关7000-7099phase-3b) 以避免段位冲突。以下错误码均从 `CommandHandlers.cpp` 的 `play_motion_ext` handler 核实。

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 3001 | `play_motion_ext` 时 `motion_path` 为空 | `"motion_path is required"` |
| 3002 | `play_motion_ext` 时动作文件不存在 | `"motion file not found: <path>"` |
| 3003 | 动作被优先级守卫拒绝或加载失败 | `"Motion rejected by priority guard or failed to load"` |
| 3004 | `play_motion_ext` 时路径包含不安全字符（`..`、`~` 等） | `"motion_path contains unsafe traversal"` |

---

## 四、窗口相关（4000-4099）

| 错误码 | 触发场景 |
|:---:|:---|
| 4001 | 坐标超出屏幕范围 |
| 4002 | 窗口创建失败 |
| 4003 | OpenGL 上下文创建失败 |
| 4004 | `set_size` 时 width/height 非正 |

---

## 五、通信相关（5000-5099）

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 5001 | 协议版本不匹配（保留） | — |
| 5002 | 消息格式错误（JSON 解析失败，保留） | — |
| 5003 | 未知 action | `"Unknown action: <action>"` |

> **实际触发**：渲染引擎收到未注册的 action 时，返回 **Response**（`type: "response"`，`id` 复用原始 Command 的 `id`，`success: false`，`error_code: 5003`）。控制面板可通过 `id` 匹配到对应的 pending request。

---

## 六、系统/通用（6000-6099）

| 错误码 | 触发场景 |
|:---:|:---|
| 6001 | handler 处理器抛出异常（`MessageHandler::dispatch` catch 块） |
| 6002 | 消息序列化失败（保留） |
| 6003 | `set_fps` 时 fps 值非法 | `"fps must be 0 (adaptive) or 1-120"` |

> 6002 为预留错误码，当前未启用。

---

## 七、音频相关（7000-7099）— Phase 3b

> 以下错误码均从 `CommandHandlers.cpp` 的 `play_audio` / `stop_audio` / `set_volume` handler 核实。音频模块基于 `AudioManager`（miniaudio + libvorbis）。相关指令详见 [Commands §10.2–10.4](./commands.md#102-play_audio--播放音频phase-3b-已实现)。

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 7001 | `play_audio` 时 `audio_path` 为空 | `"audio_path is required"` |
| 7002 | `play_audio` 或 `set_volume` 时 `AudioManager` 未初始化 | `"Audio engine not initialized"` |
| 7003 | `play_audio` 时音频文件不存在 | `"audio file not found: <path>"` |
| 7004 | `play_audio` 时路径包含不安全字符（`..`、`~` 等） | `"audio_path contains unsafe traversal"` |

> **即发即忘模式**：Phase 3b 音频播放不上报完成事件，控制面板无法收到播放结束通知（无错误码也无事件）。`stop_audio` 无论引擎状态如何均返回 `success: true`。

---

## 八、资源监视相关（9000-9099）

> 以下错误码对应资源监视模块（`renderer/src/monitor/`），协议常量 `ERROR_STATS_COLLECTION_FAILED`。相关指令详见 [Commands §11 `get_stats`](./commands.md#11-get_stats--请求资源占用快照资源监视)，事件详见 [Events §11 `stats_state`](./events.md#11-stats_state--资源占用快照)。

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 9001 | 资源采集失败（部分或全部指标不可用） | `"Resource stats collection failed"` |

> **当前实现的失败表达方式**：`get_stats` handler 在 GPU 监视器未初始化或单项采集失败时，**不返回 9001**，而是照常 emit `stats_state` 事件，将不可用字段在 payload 中置为 `null`（详见 [stats_state payload 字段表](./events.md#11-stats_state--资源占用快照)）。9001 为预留错误码，用于未来采集层全面失败时在 Response 中返回。

---

## 九、布局相关（8000-8099）

> 对应用户布局偏移指令 `set_layout` / `get_layout` / `reset_layout`。

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 8001 | `set_layout` / `get_layout` 时无模型加载 | `"No model loaded"` |

---

## 十、字幕相关（10000-10099）— ⚠️ 已废弃

> **已废弃（原字幕系统，已移除；段位保留）**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代（参见 [docs/system/notification-stream.md](../system/notification-stream.md)），渲染器不再注册字幕指令。以下错误码不再触发，未知指令统一返回 [5003](#五通信相关5000-5099)。10000 段位保留，可用于未来模块。

| 错误码 | 触发场景 | 伴随消息 |
|:---:|:---|:---|
| 10001 | ~~`show_subtitle` 时 `text` 为空或缺失~~ | ~~`"text is required"`~~ |
| 10002 | ~~字幕引擎（`SubtitleManager`）未初始化~~ | ~~`"Subtitle engine not initialized"`~~ |
