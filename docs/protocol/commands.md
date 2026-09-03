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

设置模型的显示缩放比例（`set_layout` scale 轴的兼容别名）。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `scale` | float | ✓ | 当前值 | 缩放比例，实际值钳制到 0.1–5.0 范围 |

**渲染器处理流程**：
1. 校验已加载模型，否则返回错误 `8001`
2. 保留当前用户布局偏移（`offset_x`/`offset_y` 不变），仅更新 `scale`（经 `LAppModel::SetUserLayout` 钳制后生效）
3. 发送 Response（`success: true`）

> **与 `set_layout` 的关系**：本指令历史上长期为 stub（仅记日志），现已实现为 `set_layout` scale 轴的兼容别名——只改 scale、保留当前偏移。`set_layout` 仍是一等入口（可同时合并任意布局轴）。模型缩放后点击命中自动跟随（`HitTest` 含用户布局逆变换）。

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `8001` | 当前无模型加载 | `"No model loaded"` |

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
| `motion_path` | string | ✓ | — | 动作文件绝对路径（`.motion3.json`）。缺失返回 `3001`，路径不安全返回 `3004`，文件不存在返回 `3002` |
| `priority` | int | 否 | `2` | 动作优先级（同 [play_motion 优先级常量](#2-play_motion--播放动作)） |
| `fade_in` | float | 否 | `1.0` | 淡入时长（秒） |
| `fade_out` | float | 否 | `1.0` | 淡出时长（秒） |
| `audio_path` | string | 否 | `""` | 附带音频文件绝对路径。若 `AudioManager` 已初始化且文件存在则播放（会先停止当前音频）；引擎未初始化或文件不存在时仅记录日志，不报错 |

**渲染器处理**：
1. 校验 `motion_path` 非空，否则返回 `3001`
2. 校验路径安全性（拒绝含 `..`、`~` 等穿越字符的路径），否则返回 `3004`
3. 校验文件存在，否则返回 `3002`
4. 校验已加载模型，否则返回 `2001`
5. 加载并播放动作；若被优先级守卫拒绝或加载失败，返回 `3003`
6. 若附带 `audio_path` 且 `AudioManager` 已初始化：停止当前音频 → 播放指定音频（文件不存在则仅记录日志）
7. 返回 Response（`success: true`）

> **已移除字段**：历史实现中控制面板曾随本指令发送 `subtitle_text`/`subtitle_duration` 字段，渲染器侧的相应处理分支已删除——字幕系统已由 Qt 控制器通知流（气泡信息流）取代，文档文案现经由通知流展示（参见 [docs/system/notification-stream.md](../system/notification-stream.md)）。`lip_sync_path` 字段保留（Phase 3c 口型同步待实现）。

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
| `7004` | `audio_path` 路径包含不安全字符（`..`、`~` 等） | `"audio_path contains unsafe traversal"` |

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

---

## 12. `set_size` — 设置窗口尺寸

设置渲染器窗口尺寸。窗口居中调整（保持中心点不变，同步调整窗口左上角坐标）。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `width` | int | ✓ | — | 窗口宽度（像素，正值）。实际值钳制到 100-2000 范围 |
| `height` | int | ✓ | — | 窗口高度（像素，正值）。实际值钳制到 100-2000 范围 |

**渲染器处理流程**：
1. 校验 `width`、`height` 均为正值，否则返回错误 `4004`
2. 将宽高钳制到 100-2000 范围
3. 调整窗口尺寸，保持窗口中心点不变（同步调整窗口位置坐标）
4. 发送 Response（`success: true`）

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `4004` | `width` 或 `height` 非正（≤ 0） | — |

---

## 13. `set_fps` — 设置目标帧率

设置渲染引擎目标帧率，支持自适应模式与固定帧率模式。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `fps` | number | ✓ | — | 目标帧率。`0` = 自适应模式（15-60fps 浮动），`1`-`120` = 固定帧率 |

**渲染器处理流程**：
1. 校验 `fps` 合法（等于 `0`，或落在 `1`-`120` 闭区间），否则返回错误 `6003`
2. 应用帧率模式（自适应 / 固定）
3. 发送 Response（`success: true`）

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `6003` | `fps` 为负数、落入 `(0, 1)` 开区间的小数，或大于 `120` | `"fps must be 0 (adaptive) or 1-120"` |

---

## 14. `set_hit_areas` — 设置点击区域

配置模型点击检测区域名称列表。渲染器使用这些名称与 Cubism 模型的 HitArea 定义匹配，实现点击命中检测。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `hit_areas` | string[] | ✓ | — | 点击区域名称数组（如 `["Head", "Body"]`）。名称需与模型 `.model3.json` 中的 HitArea 定义一致 |

**渲染器处理流程**：
1. 校验 `hit_areas` 字段存在且为 JSON 数组，否则返回错误 `1005`
2. 更新内部点击区域配置
3. 发送 Response（`success: true`）

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `1005` | `hit_areas` 缺失或非数组类型 | `"hit_areas array is required"` |

---

## 15. `set_layout` — 设置用户布局

设置模型用户布局偏移（手动位置与缩放调整）。与 [`set_scale`](#6-set_scale--设置模型缩放)（scale 轴兼容别名）不同，`set_layout` 可同时合并任意布局轴。

**回执**：✓ 需要 Response

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `offset_x` | float | 否 | 当前值 | 水平偏移量 |
| `offset_y` | float | 否 | 当前值 | 垂直偏移量 |
| `scale` | float | 否 | 当前值 | 缩放比例 |

> 三个字段均为可选，仅传入的字段更新对应属性，未传入字段保持当前值不变。

**渲染器处理流程**：
1. 校验已加载模型，否则返回错误 `8001`
2. 合并传入的布局参数（未传入字段保留当前值）
3. 应用新布局
4. 发送 Response（`success: true`）

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `8001` | 当前无模型加载 | `"No model loaded"` |

---

## 16. `get_layout` — 查询用户布局

查询当前模型的用户布局参数。数据通过 `layout_state` 事件回传（`get_stats`/`stats_state` 复用了此"走事件绕过空 payload Response"先例，因 `createResponse` 的 payload 固定为空对象，无法承载布局数据）。

**回执**：✗ 无 Response（布局数据通过 `layout_state` 事件回传）

**Payload**：空对象 `{}`

**事件**：发送 `layout_state` 事件，payload 字段：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `offset_x` | float | 水平偏移量 |
| `offset_y` | float | 垂直偏移量 |
| `scale` | float | 缩放比例 |

**渲染器处理流程**：
1. 校验已加载模型，否则返回错误 `8001`
2. 读取当前用户布局参数
3. 发送 `layout_state` 事件

> **不发送 Response**：handler 不调用 `createResponse`，仅 emit 事件。控制面板应在 `MessageDispatcher` 注册 `layout_state` 事件 handler 接收数据，不应为 `get_layout` 挂 pending request 等待 Response（10 秒超时后会落空）。

**错误码**：

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `8001` | 当前无模型加载 | `"No model loaded"` |

---

## 17. `reset_layout` — 重置用户布局

将模型用户布局重置为默认值（`offset_x`、`offset_y`、`scale` 恢复初始状态）。

**回执**：✓ 需要 Response

**Payload**：空对象 `{}`

**渲染器处理流程**：
1. 重置用户布局参数为默认值
2. 发送 Response（`success: true`）

---

## 18. `show_subtitle` — 显示字幕

> **已移除**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代。参见 [docs/system/notification-stream.md](../system/notification-stream.md)。渲染器不再注册此指令（未知指令返回 5003）。

---

## 19. `hide_subtitle` — 隐藏字幕

> **已移除**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代。参见 [docs/system/notification-stream.md](../system/notification-stream.md)。渲染器不再注册此指令（未知指令返回 5003）。

---

## 20. `set_subtitle_style` — 设置字幕默认样式

> **已移除**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代。参见 [docs/system/notification-stream.md](../system/notification-stream.md)。渲染器不再注册此指令（未知指令返回 5003）。

---

## 21. `set_subtitle_adjust_mode` — 进入/退出字幕调整模式

> **已移除**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代。参见 [docs/system/notification-stream.md](../system/notification-stream.md)。渲染器不再注册此指令（未知指令返回 5003）。

---

## 22. `set_subtitle_layout` — 设置字幕布局

> **已移除**：字幕系统已被 Qt 控制器通知流（气泡信息流）取代。参见 [docs/system/notification-stream.md](../system/notification-stream.md)。渲染器不再注册此指令（未知指令返回 5003）。
