# 控制面板 → 渲染器（Commands）

> 协议概述与 Envelope 格式参见 [通信协议](./README.md)。
> 渲染器→控制面板事件参见 [Events](./events.md)。

---

## 1. `load_model` — 加载模型

替换当前模型。已有模型时自动卸载旧模型再加载新模型。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `model_path` | string | ✓ | — | 模型短名称（如 `"Hiyori"`），渲染器内部构造完整路径 `Resources/<name>/<name>.model3.json` |

**示例**：

```json
{
  "type": "command", "action": "load_model",
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "payload": { "model_path": "Hiyori" },
  "timestamp": 1710000000000
}
```

**渲染器处理流程**：
1. 校验 `model_path` 非空，否则返回错误 `1001`
2. 验证模型路径 `Resources/<model_path>/` 是否存在，否则返回错误 `1001`
3. 执行模型切换（卸载旧模型 → 加载新模型）
4. 发送 Response（`success: true`）
5. 发送 `model_loaded` 事件

**成功 Response**：

```json
{ "type": "response", "action": "load_model", "id": "550e8400...", "payload": {},
  "success": true, "error_code": 0, "error_message": "", "timestamp": ... }
```

**失败 Response**：

```json
{ "type": "response", "action": "load_model", "id": "550e8400...", "payload": {},
  "success": false, "error_code": 1001, "error_message": "Model path not found: InvalidModel",
  "timestamp": ... }
```

同时发送 `model_load_failed` 事件。

---

## 2. `play_motion` — 播放动作

播放指定动作组中的动作。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `group` | string | ✓ | `""` | 动作组名称（如 `"Idle"`、`"TapBody"`、`"TapHead"`） |
| `index` | int | 否 | `0` | 动作组内索引，从 `0` 开始 |
| `priority` | int | 否 | `2` | 动作优先级（见下方优先级常量） |

> **`group` 和 `index` 的有效值**：取决于当前加载的模型。控制面板需自行解析模型定义文件获取合法的动作组名称和每组的动作数量，详见 [Events — `model_loaded`](./events.md#2-model_loaded--模型加载完成) 中的"模型能力发现"说明。

**动作优先级常量**：

| 常量 | 值 | 用途 |
|:---|:---:|:---|
| `PriorityNone` | 0 | 无优先级 |
| `PriorityIdle` | 1 | 闲时动作（可被任何动作中断） |
| `PriorityNormal` | 2 | 普通动作（点击触发等） |
| `PriorityForce` | 3 | 强制动作（不可被中断） |

**示例**：

```json
{
  "type": "command", "action": "play_motion",
  "id": "...", "payload": { "group": "TapHead", "index": 0, "priority": 2 },
  "timestamp": ...
}
```

**渲染器处理**：
- 动作播放开始时发送 `motion_started` 事件，播放结束时发送 `motion_finished` 事件
- 若当前无模型加载，发送 `error` 事件（错误码 `2001`）
- 若 `group` 不存在或 `index` 越界，静默忽略（不触发动作，不报错）

---

## 3. `stop_motion` — 停止动作

停止当前正在播放的所有动作。若无动作在播放，静默忽略。

**回执**：✗ 无 Response

**Payload**：空对象 `{}`

---

## 4. `set_expression` — 设置表情

切换模型表情。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `expression_id` | string | ✓ | `""` | 表情 ID（如 `"default"`，来源于 `.model3.json` 的 Expressions 定义） |

> **`expression_id` 的有效值**：取决于当前加载的模型，详见 [Events — `model_loaded`](./events.md#2-model_loaded--模型加载完成) 中的"模型能力发现"说明。若 `expression_id` 不存在，静默忽略。

---

## 5. `set_position` — 设置窗口位置

设置渲染器窗口在屏幕上的位置。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `x` | int | ✓ | `0` | 窗口左上角 X 坐标（像素，屏幕坐标系，原点为屏幕左上角） |
| `y` | int | ✓ | `0` | 窗口左上角 Y 坐标（像素，Y 轴向下） |

**渲染器处理**：直接应用坐标值，不做范围校验。负值或超出屏幕的坐标会导致窗口部分或完全不可见。

---

## 6. `set_scale` — 设置模型缩放

设置模型的显示缩放比例。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `scale` | float | ✓ | `1.0` | 缩放比例 |

> ⚠️ **当前状态：stub**。该指令已在 `CommandHandlers.cpp` 注册但**未实际实现缩放**，收到后仅调用 `LAppPal::PrintLogLn` 记录日志（输出 `"set_scale: <value> (not fully implemented)"`），不产生实际效果。模型缩放请改用 `set_layout`（通过 `scale` 字段调用 `LAppModel::SetUserLayout`）。

---

## 7. `set_opacity` — 设置窗口透明度

设置渲染器窗口的整体透明度。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `opacity` | float | ✓ | `1.0` | 透明度。`0.0` = 完全透明，`1.0` = 完全不透明 |

**渲染器处理**：直接应用透明度值，不做范围校验。超出 `0.0-1.0` 范围的行为取决于底层窗口系统。

---

## 8. `hello` — 握手

控制面板向渲染器发送的握手消息。

**回执**：✗ 无 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `client_name` | string | 否 | `"unknown"` | 客户端标识 |
| `version` | string | 否 | — | 协议版本 |

> **说明**：当前握手流程中控制面板不主动发送 `hello`，而是等待渲染器的 `ready` 事件后直接发送 `load_model`。`hello` 指令保留用于未来版本协商。渲染器收到后仅记录日志。

---

## 9. `shutdown` — 优雅关闭

请求渲染器优雅关闭。

**回执**：✓ 需要 Response

**Payload**：空对象 `{}`

**渲染器处理**：先发送 Response（`success: true`），然后触发渲染主循环退出。控制面板收到 Response 后应预期 WebSocket 连接即将关闭。

---

## 10. Phase 3 指令（✅ 已实现）

Phase 3 指令分两批在 `renderer/src/network/CommandHandlers.cpp` 中注册实现：

- **Phase 3a**（外置语音包挂载）：`play_motion_ext` 已实现，支持从外部绝对路径加载 `.motion3.json` 动作并可选附带音频。详见 [外置语音包挂载](../system/voice-pack-mounting.md)。
- **Phase 3b**（音频播放）：`play_audio`、`stop_audio`、`set_volume` 已实现，接入 `AudioManager`（miniaudio + libvorbis，OGG 播放）。音频播放为**即发即忘**模式，无播放完成事件上报。

> **未实现的早期设计**：早期文档中设计的 `set_audio_mapping`、`set_mute` 两条指令当前**未注册**。其中 `set_mute` 的功能已合并进 `set_volume`（通过 `muted` 字段实现）；`set_audio_mapping` 属于控制器侧映射管理概念（`AudioMappingManager`），渲染器侧不需要。

### 10.1 `play_motion_ext` — 播放外部动作文件（Phase 3a ✅ 已实现）

从外部绝对路径加载并播放 `.motion3.json` 动作文件，支持自定义优先级与淡入淡出，可选附带音频播放。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `motion_path` | string | ✓ | — | 动作文件绝对路径（`.motion3.json`）。缺失返回 `3001`，文件不存在返回 `3002` |
| `priority` | int | 否 | `2` | 动作优先级（同 [play_motion 优先级常量](#2-play_motion--播放动作)） |
| `fade_in` | float | 否 | `1.0` | 淡入时长（秒） |
| `fade_out` | float | 否 | `1.0` | 淡出时长（秒） |
| `audio_path` | string | 否 | `""` | 附带音频文件绝对路径。若 `AudioManager` 已初始化且文件存在则播放（会先停止当前音频）；引擎未初始化或文件不存在时仅记录日志，不报错 |

**渲染器处理**：
1. 校验 `motion_path` 非空，否则返回 `3001`
2. 校验文件存在，否则返回 `3002`
3. 校验已加载模型，否则返回 `2001`
4. 加载并播放动作；若被优先级守卫拒绝或加载失败，返回 `3003`
5. 若附带 `audio_path` 且 `AudioManager` 已初始化：停止当前音频 → 播放指定音频（文件不存在则仅记录日志）
6. 返回 Response（`success: true`）

**事件**：动作开始时不发送 `motion_started`；动作播放完成时发送 `motion_finished` 事件，**payload 与 `play_motion` 不同**——使用 `{ "motion_path": "<原路径>" }` 而非 `{ "group", "index" }`。详见 [Events §10](./events.md#10-phase-3-事件)。

---

### 10.2 `play_audio` — 播放音频（Phase 3b ✅ 已实现）

播放独立音频文件（OGG），接入 `AudioManager`（miniaudio + libvorbis）。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `audio_path` | string | ✓ | — | 音频文件绝对路径。缺失返回 `7001`，文件不存在返回 `7003` |
| `volume` | float | 否 | `1.0` | 本次播放音量（`0.0`-`1.0`） |

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `7001` | `audio_path` 为空 | `"audio_path is required"` |
| `7002` | `AudioManager` 未初始化 | `"Audio engine not initialized"` |
| `7003` | 音频文件不存在 | `"audio file not found: <path>"` |

> **即发即忘**：音频播放完成不上报事件（无 `audio_started`/`audio_ended` 事件，见 [Events §10](./events.md#10-phase-3-事件)）。

---

### 10.3 `stop_audio` — 停止音频（Phase 3b ✅ 已实现）

停止所有正在播放的音频（调用 `AudioManager::StopAll()`）。

**回执**：✓ 需要 Response

**Payload**：空对象 `{}`

> 无论 `AudioManager` 是否初始化，均返回 `success: true`（未初始化时静默跳过）。

---

### 10.4 `set_volume` — 设置音量/静音（Phase 3b ✅ 已实现）

设置全局音量和/或静音状态。早期设计的 `set_mute` 指令功能已合并至此。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `volume` | float | 否 | `1.0` | 播放音量（`0.0`-`1.0`）。字段存在时调用 `AudioManager::SetVolume` |
| `muted` | bool | 否 | `false` | 是否静音。字段存在时调用 `AudioManager::SetMuted` |

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `7002` | `AudioManager` 未初始化 | `"Audio engine not initialized"` |

> 两个字段均为可选，控制面板按需传入；字段不存在时对应属性保持不变。

---

## 11. `get_stats` — 请求资源占用快照（资源监视）

请求渲染器上报当前进程的资源占用快照（CPU、内存、GPU 利用率、显存），用于控制面板"资源监视"页面实时显示渲染引擎侧指标。控制面板以固定周期（默认 2 秒）轮询发送。

**回执**：✗ 无 Response（资源数据通过 [`stats_state`](./events.md#11-stats_state--资源占用快照) 事件回传——复用 `get_layout`/`layout_state` 的"走事件绕过空 payload Response"先例，因 `createResponse` 的 payload 固定为空对象，无法承载指标数据）

**Payload**：空对象 `{}`

**示例**：

```json
{
  "type": "command", "action": "get_stats",
  "id": "...", "payload": {}, "timestamp": ...
}
```

**渲染器处理流程**：
1. 在主渲染线程采集进程 CPU/RSS（`ProcessStatsCollector`）与 GPU 利用率/显存（`IGpuMonitor`）。handler 经 `LAppDelegate::Run` 的 `PollNetworkMessages` → `drainMessages` 派发，**主线程执行**，不从 WebSocket 回调线程调用任何系统采集 API（PDH/DXGI/psapi，项目铁律）。
2. 构造 payload（字段见 [Events — `stats_state`](./events.md#11-stats_state--资源占用快照)）。GPU 监视器未初始化（如 Linux Stub）时 4 个 GPU 相关字段（`gpu_percent`、`gpu_name`、`vram_used_bytes`、`vram_total_bytes`）置为 `null`；已初始化但单项采集失败时对应字段置 `null`。CPU/RSS/时间戳始终可用。
3. 发送 `stats_state` 事件（`sendResponse(createEvent("stats_state", payload))`）。

> **不发送 Response**：handler 不调用 `createResponse`，仅 emit 事件。控制面板应在 `MessageDispatcher` 注册 `stats_state` 事件 handler 接收数据，**不应**为 `get_stats` 挂 pending request 等待 Response（10 秒超时后会落空）。

> **平台差异**：Windows 经 PDH（GPU%）+ DXGI（VRAM，per-process `QueryVideoMemoryInfo`）采集；Linux 为 Stub，`IGpuMonitor::initialize()` 返回 false，4 个 GPU 字段恒为 `null`。Vulkan 变体的 VRAM 数值依赖 WDDM 驱动报告，可能偏低或为 0（已知限制，非回归）。
