# 渲染器 → 控制面板（Events）

> 协议概述与 Envelope 格式参见 [通信协议](./README.md)。
> 控制面板→渲染器指令参见 [Commands](./commands.md)。

---

## 1. `ready` — 渲染器就绪

渲染器 WebSocket 连接建立后主动发送，通知控制面板可以开始下发指令。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `version` | string | 渲染器协议版本（当前为 `"1.0.0"`） |
| `capabilities` | string[] | 渲染器支持的能力列表（当前为 `["live2d"]`） |

**示例**：

```json
{
  "type": "event", "action": "ready",
  "id": "...", "payload": { "version": "1.0.0", "capabilities": ["live2d"] },
  "timestamp": ...
}
```

**控制面板预期行为**：
1. 标记连接状态为已连接
2. 发送 `load_model` 指令（使用当前配置的模型名称）
3. 等待 `load_model` 的 Response 回执（建议 10 秒超时）
4. 发送 `set_position` 指令（使用持久化的窗口位置）
5. 重放断连期间缓存的关键指令（见 [断连缓存策略](./implementation.md)）

---

## 2. `model_loaded` — 模型加载完成

模型成功加载后渲染器发送。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `model_id` | string | 模型短名称（与 `load_model` 的 `model_path` 一致） |
| `motions` | array | 动作组列表。⚠️ **当前始终为空数组 `[]`**，见下方"模型能力发现" |
| `expressions` | array | 表情列表。⚠️ **当前始终为空数组 `[]`**，见下方"模型能力发现" |

**示例**：

```json
{
  "type": "event", "action": "model_loaded",
  "id": "...", "payload": { "model_id": "Hiyori", "motions": [], "expressions": [] },
  "timestamp": ...
}
```

**控制面板预期行为**：记录当前已加载的模型名称，加载该模型对应的本地配置（如闲时行为策略、交互映射等）。

### 模型能力发现

由于 `motions` 和 `expressions` 字段当前始终为空，控制面板需**自行解析模型定义文件**以获取可用的动作组和表情列表。

**模型定义文件位置**：`Resources/<model_id>/<model_id>.model3.json`

其中 `<model_id>` 为 `model_loaded` 事件中的 `model_id` 值（即 `load_model` 时传入的 `model_path`）。

**文件格式**（Live2D Cubism `.model3.json`，仅列出协议相关部分）：

```json
{
  "FileReferences": {
    "Motions": {
      "Idle": [
        { "File": "motions/idle_01.motion3.json" },
        { "File": "motions/idle_02.motion3.json" }
      ],
      "TapBody": [
        { "File": "motions/tapBody_01.motion3.json" }
      ],
      "TapHead": [
        { "File": "motions/tapHead_01.motion3.json" }
      ]
    },
    "Expressions": [
      { "Name": "default", "File": "expressions/default.exp3.json" },
      { "Name": "angry", "File": "expressions/angry.exp3.json" }
    ]
  }
}
```

**解析规则**：

| 需要的数据 | 提取方式 | 用途 |
|:---|:---|:---|
| 动作组名称 | `FileReferences.Motions` 的所有 key | `play_motion` 的 `group` 参数 |
| 每组动作数量 | 每个 key 对应数组的长度 | `play_motion` 的 `index` 参数（`0` ~ `length-1`） |
| 表情 ID | `FileReferences.Expressions[*].Name` | `set_expression` 的 `expression_id` 参数 |

> **注意**：模型定义文件位于渲染引擎的工作目录下。控制面板需能够访问该路径。不同模型的动作组和表情列表完全不同，**必须在模型加载后重新解析**。

---

## 3. `model_load_failed` — 模型加载失败

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `error_code` | int | 错误码（见 [错误码](./error-codes.md)） |
| `error_message` | string | 错误描述 |

---

## 4. `motion_started` — 动作开始

动作开始播放时渲染器发送。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `group` | string | 动作组名称 |
| `index` | int | 动作组内索引 |

**控制面板预期行为**：可用于 UI 状态展示或日志记录。非必须处理。

---

## 5. `motion_finished` — 动作结束

动作播放完成后渲染器发送。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `group` | string | 动作组名称 |
| `index` | int | 动作组内索引 |

**控制面板预期行为**：可用于追踪动作播放状态（如判断闲时动作是否结束、是否需要触发下一个动作）。非必须处理。

---

## 6. `hit` — 点击命中

用户点击模型 HitArea 时渲染器发送。仅在点击命中模型区域时触发（未命中时触发拖拽）。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `area_id` | string | 命中区域标识。当前支持：`"head"`、`"body"`（**小写**） |
| `x` | float | 鼠标 X 坐标（窗口客户区坐标） |
| `y` | float | 鼠标 Y 坐标（窗口客户区坐标） |
| `button` | int | 鼠标按键。`0` = 左键 |

**HitArea 判定逻辑**：
1. 鼠标按下时，渲染引擎将屏幕坐标转换为模型本地坐标
2. 判断是否命中模型区域
3. 命中 → 细分区域：命中头部 → `area_id = "head"`，其他部位 → `area_id = "body"`
4. 未命中模型区域 → 进入拖拽模式（不发送 `hit` 事件）

> **`area_id` 取值**：当前固定为 `"head"` 或 `"body"`（小写），由模型的 HitArea 定义决定。未来可能扩展更多区域。

**控制面板预期行为**：
- 根据 `area_id` 查找交互映射配置，确定应播放的动作组
- 建议默认映射：`"head"` → `TapHead`、`"body"` → `TapBody`
- 构建 `play_motion` command（`group` = 映射结果，`index` = 0，`priority` = 2）

---

## 7. `drag_start` — 拖拽开始

用户按下鼠标且未命中模型 HitArea 时触发。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 拖拽起始鼠标 X 坐标（窗口客户区） |
| `y` | float | 拖拽起始鼠标 Y 坐标 |

**控制面板预期行为**：记录拖拽状态为"进行中"，作为后续 `drag_end` 事件的前置条件。

---

## 8. `drag_end` — 拖拽结束

用户释放鼠标且之前处于拖拽状态时触发。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 释放时鼠标 X 坐标（窗口客户区） |
| `y` | float | 释放时鼠标 Y 坐标 |
| `window_x` | int | 窗口最终左上角 X 坐标（**屏幕像素坐标**） |
| `window_y` | int | 窗口最终左上角 Y 坐标 |

**拖拽状态防护**：
- 渲染器端：仅在之前处于拖拽状态时才发送 `drag_end` 事件
- 控制面板端：仅在收到过对应 `drag_start` 的情况下处理 `drag_end`

**控制面板预期行为**：提取 `window_x`/`window_y` → 更新运行状态中的窗口位置 → 持久化到配置文件（下次启动时通过 `set_position` 恢复）。

---

## 9. `error` — 错误上报

渲染器运行时错误。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `error_code` | int | 错误码（见 [错误码](./error-codes.md)） |
| `error_message` | string | 错误描述 |

---

## 10. Phase 3 事件

Phase 3 **无新增独立事件**，已通过源码核实（`EventEmitter::emit()` 调用点遍布 `LAppDelegate.cpp` / `LAppLive2DManager.cpp` / `LAppModel.cpp` / `CommandHandlers.cpp`，均无音频相关 emit）：

- **Phase 3a**（`play_motion_ext`）：**复用现有的 `motion_finished` 事件**，但 payload 与 `play_motion` 不同——使用 `{ "motion_path": "<动作文件路径>" }` 而非 `{ "group", "index" }`（由 `LAppModel.cpp:763` 的 `OnExtMotionFinishedStatic` 回调发出）。详见 [Commands §10.1](./commands.md#101-play_motion_ext--播放外部动作文件phase-3a-已实现)。
- **Phase 3b**（音频播放）：采用**即发即忘（fire-and-forget）**模式，`AudioManager` 播放完成不上报事件。早期设计的 `audio_started`/`audio_ended` 事件**未实现**。

| 早期设计 action | 状态 | 说明 |
|:---|:---:|:---|
| `audio_started` | ❌ 未实现 | 早期设计草案，Phase 3b 实现时改为即发即忘，不上报 |
| `audio_ended` | ❌ 未实现 | 同上 |

> **事件总览**：当前渲染器实际发出的事件共 13 个——`ready`、`model_loaded`、`model_load_failed`、`motion_started`、`motion_finished`（两种 payload 变体）、`hit`、`drag_start`、`drag_end`、`layout_changed`、`window_resized`、`layout_state`（`get_layout` 命令的响应事件）、`stats_state`（`get_stats` 命令的响应事件）、`error`。

---

## 11. `stats_state` — 资源占用快照

渲染器进程资源占用快照，作为 [`get_stats`](./commands.md#11-get_stats--请求资源占用快照资源监视) 命令的响应事件发送（每收到一条 `get_stats` 即 emit 一条 `stats_state`）。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `cpu_percent` | number | 渲染器进程 CPU 利用率百分比（`0.0`-`100.0`，多线程时可大于 100）。**始终可用** |
| `rss_bytes` | number | 渲染器进程常驻内存（RSS），字节数。**始终可用** |
| `gpu_percent` | number\|null | GPU 利用率百分比。Windows 经 PDH（`\\GPU Engine(*)\\Utilization Percentage`，按 `pid_<本进程>` 前缀过滤求和）采集；Linux Stub 或采集失败时为 `null` |
| `gpu_name` | string\|null | GPU 适配器名称（如 `"NVIDIA GeForce RTX 3060"`，Windows 取自 `DXGI_ADAPTER_DESC.Description`）。GPU 监视器未初始化时为 `null`；Linux Stub 初始化失败时同样为 `null` |
| `vram_used_bytes` | number\|null | 已用显存，字节数。Windows 经 DXGI `IDXGIAdapter3::QueryVideoMemoryInfo` 的 `CurrentUsage`（per-process）采集；Linux Stub 或采集失败时为 `null`。⚠️ **Vulkan 变体数值依赖 WDDM 驱动报告**，`vkAllocateMemory` 的分配能否被 DXGI 计入是驱动相关的，可能偏低或为 0（已知限制，非回归） |
| `vram_total_bytes` | number\|null | 显存总量，字节数（`DXGI_ADAPTER_DESC.DedicatedVideoMemory`）。同上可空 |
| `timestamp_ms` | number | 渲染器侧采集时间戳（Unix 毫秒）。**始终可用** |

**示例**（Windows，全字段）：

```json
{
  "type": "event", "action": "stats_state",
  "id": "...",
  "payload": {
    "cpu_percent": 12.5,
    "rss_bytes": 89128960,
    "gpu_percent": 35.0,
    "gpu_name": "NVIDIA GeForce RTX 3060",
    "vram_used_bytes": 104857600,
    "vram_total_bytes": 8589934592,
    "timestamp_ms": 1719990000000
  },
  "timestamp": ...
}
```

**示例**（Linux Stub，GPU 字段全 `null`）：

```json
{
  "type": "event", "action": "stats_state",
  "id": "...",
  "payload": {
    "cpu_percent": 8.2,
    "rss_bytes": 72351744,
    "gpu_percent": null,
    "gpu_name": null,
    "vram_used_bytes": null,
    "vram_total_bytes": null,
    "timestamp_ms": 1719990000000
  },
  "timestamp": ...
}
```

**控制面板预期行为**：
- WebSocket server 线程收到 → 反序列化为 `RendererStats` record（`fromJson` 接受字段缺失或显式 `null` 两种 null 表达形式）
- 经 `Platform.runLater` 切到 JavaFX 线程更新 `MonitorDataModel`（线程边界遵循 `MainWindowController` 的 WS→FX 模式）
- `null` 字段在 UI 显示"—"/"不可用"（灰色）
- 陈旧判定：超过 10 秒（5 个轮询周期）未收到 `stats_state`，"最后更新"Label 显示"⚠ 数据陈旧"，恢复接收时清除

> **`id` 不用于 request-response 匹配**：`stats_state` 是事件而非 Response，其 `id` 由渲染器新建（`createEvent` 生成），**不复用** `get_stats` 的 `id`。控制面板按 action 类型路由，不按 `id` 匹配 pending request。

---

## 12. `layout_changed` — 用户布局变更

用户通过交互（Shift+拖拽模型、Shift+滚轮缩放）改变模型布局后，渲染器发送当前布局参数。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `offset_x` | float | 水平偏移量 |
| `offset_y` | float | 垂直偏移量 |
| `scale` | float | 缩放比例 |

**示例**：

```json
{
  "type": "event", "action": "layout_changed",
  "id": "...", "payload": { "offset_x": 0.1, "offset_y": -0.05, "scale": 1.2 },
  "timestamp": ...
}
```

**控制面板预期行为**：提取 `offset_x`/`offset_y`/`scale` → 更新实例的布局属性 → 持久化到配置文件（下次启动时通过 `set_layout` 恢复）。

> **触发场景**：渲染器在 `LAppDelegate` 中检测到 Shift+鼠标拖拽结束（模型拖拽）或 Shift+滚轮缩放时发出。与 `set_layout` 指令形成双向同步——控制面板可通过指令设置布局，用户交互变更也会通过此事件回传。

---

## 13. `window_resized` — 窗口尺寸变更

用户通过交互（Ctrl+滚轮）缩放窗口后，渲染器发送新的窗口尺寸与位置。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `window_width` | int | 新窗口宽度（像素） |
| `window_height` | int | 新窗口高度（像素） |
| `window_x` | int | 新窗口左上角 X 坐标（屏幕坐标系，保持中心点不变） |
| `window_y` | int | 新窗口左上角 Y 坐标 |

**示例**：

```json
{
  "type": "event", "action": "window_resized",
  "id": "...", "payload": { "window_width": 400, "window_height": 600, "window_x": 1200, "window_y": 300 },
  "timestamp": ...
}
```

**控制面板预期行为**：提取 `window_width`/`window_height`/`window_x`/`window_y` → 更新实例的窗口尺寸与位置属性 → 持久化到配置文件（下次启动时通过 `set_size` 和 `set_position` 恢复）。

> **窗口居中缩放**：渲染器在调整窗口尺寸时保持中心点不变（同步调整窗口左上角坐标），因此 payload 中同时包含尺寸和位置。

---

## 14. `layout_state` — 当前布局参数查询响应

作为 [`get_layout`](./commands.md#16-get_layout--查询用户布局) 命令的响应事件发送（每收到一条 `get_layout` 即 emit 一条 `layout_state`）。复用 `get_stats`/`stats_state` 的"走事件绕过空 payload Response"模式——因 `createResponse` 的 payload 固定为空对象，无法承载布局数据。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `offset_x` | float | 水平偏移量 |
| `offset_y` | float | 垂直偏移量 |
| `scale` | float | 缩放比例 |

**示例**：

```json
{
  "type": "event", "action": "layout_state",
  "id": "...", "payload": { "offset_x": 0.0, "offset_y": 0.0, "scale": 1.0 },
  "timestamp": ...
}
```

> **与 `layout_changed` 的区别**：`layout_changed` 由用户交互触发（被动通知），`layout_state` 由控制面板主动查询触发（请求-响应）。两者 payload 结构相同，但语义不同。

> **`id` 不用于 request-response 匹配**：`layout_state` 是事件而非 Response，其 `id` 由渲染器新建（`createEvent` 生成），**不复用** `get_layout` 的 `id`。控制面板按 action 类型路由，不按 `id` 匹配 pending request。
