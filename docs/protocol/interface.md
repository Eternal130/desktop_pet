# 协议接口规格

> **文档定位**：本文档是 WebSocket 通信协议的**自包含、完整接口规格**，涵盖所有命令与事件的字段定义、类型约定、交互模式。读完本文档即可在任意语言中实现协议层，无需跳转到其他文档。
>
> **与同级文档的关系**：
> - 本文档为**主接口参考**（完整字段类型 + 功能分组 + 配对关系）
> - [commands.md](./commands.md) / [events.md](./events.md) 为按编号排列的详细参考（保留）
> - [handshake.md](./handshake.md) 为握手时序图专题
> - [error-codes.md](./error-codes.md) 为错误码专题
> - [implementation.md](./implementation.md) 为断连缓存策略与双端实现接口
> - [README.md](./README.md) 为高层协议概述
>
> **版本**：协议 v1.0.0 · 渲染器能力 `["live2d"]`

---

## 一、类型约定

> ⚠️ **本节是实现协议层前必须通读的内容**。JSON 原生只有 6 种类型（string/number/boolean/null/array/object），但协议中许多字段有更精确的语义要求（整数 vs 浮点、无符号颜色、64 位时间戳等）。不遵守这些约定会导致跨语言互通失败。

### 1.1 协议类型 → JSON 映射

| 协议类型 | JSON 表示 | 语义说明 | 示例值 |
|:---|:---|:---|:---|
| `string` | string | UTF-8 字符串 | `"Hiyori"` |
| `int` | number（整数） | 32 位有符号整数。**序列化时必须为整数形式**（`1200`），不可为 `1200.0` | `1200` |
| `int64` | number（整数） | 64 位有符号整数。⚠️ **JavaScript 精度警告**：超过 `Number.MAX_SAFE_INTEGER`（2^53-1）会丢精度，建议用 string 或 BigInt 传递 | `1710000000000` |
| `float` | number | 32 位浮点数。JSON 不区分 float/double | `0.85` |
| `double` | number | 64 位浮点数（双精度）。JSON 中与 float 同型 | `48.0` |
| `bool` | boolean | 布尔值，必须为 JSON `true`/`false`，不可用 `0`/`1` | `true` |
| `uint32` | number（非负整数） | 32 位无符号整数。**专用于颜色字段**（见 §1.3） | `16777215` |
| `array<T>` | array | 同质数组，元素类型为 T | `["Head", "Body"]` |
| `object` | object | JSON 对象。无结构约束时使用 | `{"k": "v"}` |
| `T?`（可空） | T 或 null | 字段值可为 `null` 或直接**省略键**。两种形式实现都必须接受 | `null` |

### 1.2 各语言类型映射建议

| 协议类型 | Java | C++ | TypeScript | Python | C# |
|:---|:---|:---|:---|:---|:---|
| `string` | `String` | `std::string` | `string` | `str` | `string` |
| `int` | `int` | `int32_t` | `number` | `int` | `int` |
| `int64` | `long` | `int64_t` | `number` \| `string`* | `int` | `long` |
| `float` / `double` | `float` / `double` | `float` / `double` | `number` | `float` | `float` / `double` |
| `bool` | `boolean` | `bool` | `boolean` | `bool` | `bool` |
| `uint32` | `long` | `uint32_t` | `number` | `int` | `uint` |
| `array<T>` | `List<T>` | `std::vector<T>` | `T[]` | `list[T]` | `List<T>` |
| `T?`（可空） | `T`（nullable） | `std::optional<T>` | `T` \| `null` | `Optional[T]` | `T?` |

> *\*TypeScript 中 `int64` 超过 `Number.MAX_SAFE_INTEGER` 时应改用 `string` 传输并在两端自行解析，或使用 `BigInt`。本协议中 `timestamp`（Unix 毫秒）和 `duration`（毫秒）均为此类型，当前值域（约 1.7 万亿）在 `Number.MAX_SAFE_INTEGER`（约 9 千万亿）范围内，**短期安全**，但 2058 年后 timestamp 会越界。*

### 1.3 颜色字段格式

协议中颜色字段（`primary_color`、`outline_color`、`shadow_color`、`bg_box_color`）统一使用 `uint32` 类型。格式为 **AABBGGRR**（与 ASS 字幕引擎的原生格式一致）：

| 字节 | AA | BB | GG | RR |
|:---|:---|:---|:---|:---|
| 含义 | Alpha（透明度） | Blue | Green | Red |
| 语义 | `0x00` = 不透明，`0xFF` = 全透明 | 0–255 | 0–255 | 0–255 |

**常见颜色示例**：

| 颜色 | uint32 值 | 说明 |
|:---|:---|:---|
| 不透明白 | `0x00FFFFFF` | AA=00(不透明), B=FF, G=FF, R=FF |
| 不透明黑 | `0x00000000` | AA=00, B=00, G=00, R=00 |
| 50% 透明黑 | `0x80000000` | AA=80(半透明), B=00, G=00, R=00 |
| 不透明红 | `0x000000FF` | AA=00, B=00, G=00, R=FF |

> ⚠️ **文档历史不一致**：部分早期文档与代码注释曾描述为"RRGGBBTT"格式（TT=透明度）。经核实代码实际默认值（如 `primary_color = 0x00FFFFFF` 表示"不透明白"），**实际生效格式为 AABBGGRR**。新实现应以本节为准。

### 1.4 可空性约定

协议中标记为 `T?` 的字段有两种合法的 JSON 表示形式，**反序列化端必须同时接受**：

```json
// 形式 A：显式 null
{ "gpu_percent": null, "cpu_percent": 12.5 }

// 形式 B：省略键
{ "cpu_percent": 12.5 }
```

序列化端可选择任一形式，但建议：**有值时写入实际值，无值时写入 `null`**（而非省略），便于消费端区分"明确为空"与"未提供"。

### 1.5 Payload 通用规则

| 规则 | 说明 |
|:---|:---|
| `payload` 字段**始终存在** | 即使无数据，也必须为 `{}`（空对象），**绝不省略**、**绝不为 `null`** |
| 字段命名 | Payload 内部字段统一使用 **`snake_case`**（如 `model_path`、`area_id`、`cpu_percent`） |
| 未知字段容忍 | 反序列化时应**忽略** payload 中未识别的字段，不报错（向前兼容） |
| 缺失字段处理 | 可选字段缺失时使用默认值；必填字段缺失时，命令返回错误 Response 或事件被丢弃 |

---

## 二、消息信封（Envelope）

所有 WebSocket 消息共享统一信封格式。权威定义见 [README.md §二](./README.md#二消息-envelope-格式)。

### 2.1 基础 Envelope（command / event）

```json
{
  "type": "command",
  "action": "play_motion",
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "payload": { "group": "Idle", "index": 0, "priority": 1 },
  "timestamp": 1710000000000
}
```

| 字段 | 协议类型 | 必填 | 说明 |
|:---|:---|:---:|:---|
| `type` | string | ✓ | `"command"` / `"event"` / `"response"` |
| `action` | string | ✓ | 操作名（如 `"load_model"`、`"hit"`） |
| `id` | string | ✓ | 唯一标识，用于 request-response 匹配。≤ 64 字符 |
| `payload` | object | ✓ | 消息负载。**始终为 JSON 对象**，空时为 `{}` |
| `timestamp` | int64 | ✓ | Unix 毫秒时间戳 |

### 2.2 Response Envelope（关键约定）

> ⚠️ **最易出错的设计**：`success`、`error_code`、`error_message` 位于 **JSON 顶层**，**不在 `payload` 内**。Response 的 `payload` **始终为 `{}`**。

```json
{
  "type": "response",
  "action": "load_model",
  "id": "<原始 command 的 id>",
  "payload": {},
  "timestamp": 1710000000100,
  "success": true,
  "error_code": 0,
  "error_message": ""
}
```

| 字段 | 协议类型 | 说明 |
|:---|:---|:---|
| `success` | bool | 操作是否成功 |
| `error_code` | int | 成功为 `0`，失败为具体错误码（见 [error-codes.md](./error-codes.md)） |
| `error_message` | string | 成功为 `""`，失败为描述文本 |

### 2.3 序列化规则

| 规则 | 说明 |
|:---|:---|
| **条件写入** | `success`/`error_code`/`error_message` **仅在 `type == "response"` 时写入** JSON。command 和 event 的 JSON **不含**这三个字段 |
| **id 复用** | Response 的 `id` 复用原始 Command 的 `id`，用于匹配 pending request |
| **payload 不空** | 任何消息的 `payload` 至少为 `{}` |

### 2.4 反序列化规则

| 规则 | 说明 |
|:---|:---|
| **必填校验** | `type`/`action`/`id`/`payload`/`timestamp` 任一缺失 → 视为无效消息，**静默丢弃** |
| **JSON 错误** | JSON 解析失败 → 静默丢弃，不抛异常 |
| **Response 字段** | 非 Response 消息的 `success`/`error_code`/`error_message` 视为不存在（置 null） |
| **超时清理** | pending request 建议设置 **10 秒超时**，超时后清理 |

### 2.5 消息方向

| type | 方向 |
|:---|:---|
| `command` | 控制面板 → 渲染器 |
| `event` | 渲染器 → 控制面板 |
| `response` | 渲染器 → 控制面板（回复 command） |

---

## 三、命令总览（控制面板 → 渲染器）

### 3.1 按功能分组目录

共 **25 条命令**（含 2 条保留/stub）。按功能分为 8 组：

| 组 | 命令 | 用途 | 回执 |
|:---|:---|:---|:---:|
| **A. 初始化** | [`load_model`](#a1-load_model--加载模型) | 加载/切换模型 | ✓ |
| | [`set_position`](#a2-set_position--设置窗口位置) | 设置窗口位置 | ✗ |
| | [`set_size`](#a3-set_size--设置窗口尺寸) | 设置窗口尺寸 | ✓ |
| | [`set_opacity`](#a4-set_opacity--设置窗口透明度) | 设置窗口透明度 | ✗ |
| | [`set_fps`](#a5-set_fps--设置目标帧率) | 设置帧率模式 | ✓ |
| | [`set_hit_areas`](#a6-set_hit_areas--设置点击区域) | 配置点击检测区域 | ✓ |
| **B. 动作与表情** | [`play_motion`](#b1-play_motion--播放动作) | 播放模型内置动作 | ✗ |
| | [`play_motion_ext`](#b2-play_motion_ext--播放外部动作文件-phase-3a) | 播放外部动作文件 + 音频 | ✓ |
| | [`stop_motion`](#b3-stop_motion--停止动作) | 停止所有动作 | ✗ |
| | [`set_expression`](#b4-set_expression--设置表情) | 切换模型表情 | ✗ |
| **C. 音频（Phase 3b）** | [`play_audio`](#c1-play_audio--播放音频-phase-3b) | 播放独立音频 | ✓ |
| | [`stop_audio`](#c2-stop_audio--停止音频-phase-3b) | 停止所有音频 | ✓ |
| | [`set_volume`](#c3-set_volume--设置音量静音-phase-3b) | 设置音量/静音 | ✓ |
| **D. 字幕** | [`show_subtitle`](#d1-show_subtitle--显示字幕) | 显示一条字幕 | ✓ |
| | [`hide_subtitle`](#d2-hide_subtitle--隐藏字幕) | 隐藏所有字幕 | ✓ |
| | [`set_subtitle_style`](#d3-set_subtitle_style--设置字幕默认样式) | 设置默认字幕样式 | ✓ |
| | [`set_subtitle_adjust_mode`](#d4-set_subtitle_adjust_mode--字幕调整模式) | 进入/退出调整模式 | ✓ |
| | [`set_subtitle_layout`](#d5-set_subtitle_layout--设置字幕布局) | 设置字幕位置与区域 | ✓ |
| **E. 布局** | [`set_layout`](#e1-set_layout--设置用户布局) | 设置模型偏移与缩放 | ✓ |
| | [`get_layout`](#e2-get_layout--查询用户布局) | 查询当前布局（响应走事件） | ✗ |
| | [`reset_layout`](#e3-reset_layout--重置用户布局) | 重置布局为默认 | ✓ |
| **F. 监控** | [`get_stats`](#f1-get_stats--请求资源占用快照) | 请求资源快照（响应走事件） | ✗ |
| **G. 生命周期** | [`shutdown`](#g1-shutdown--优雅关闭) | 请求渲染器优雅关闭 | ✓ |
| **H. 保留/Stub** | [`hello`](#h1-hello--握手保留) | 版本协商（保留，不发送） | ✗ |
| | [`set_scale`](#h2-set_scale--设置缩放-stub) | ⚠️ **stub**，用 `set_layout` 替代 | ✗ |

### 3.2 命令-事件配对关系

某些命令的"回执"不是 Response 而是独立事件，以下为配对清单：

| 命令 | 配对事件 | 说明 |
|:---|:---|:---|
| `load_model` | `model_loaded` / `model_load_failed` | 模型加载成功/失败通知（除 Response 外额外发送） |
| `play_motion` | `motion_started` → `motion_finished` | 动作开始/结束通知 |
| `play_motion_ext` | `motion_finished`（payload 变体 B） | 仅结束通知，不发送 `motion_started` |
| `get_stats` | `stats_state` | 资源快照（**不走 Response**，因 payload 固定为 `{}`） |
| `get_layout` | `layout_state` | 布局参数（**不走 Response**，同上） |

> ⚠️ **关键**：`get_stats` 和 `get_layout` 的响应通过**事件**回传，事件的 `id` 由渲染器新建，**不复用**命令的 `id`。控制面板**必须按 `action` 路由**，不能按 `id` 匹配 pending request。

---

## 四、事件总览（渲染器 → 控制面板）

### 4.1 按功能分组目录

共 **13 个事件**。按功能分为 6 组：

| 组 | 事件 | 触发条件 | 控制面板响应 |
|:---|:---|:---|:---|
| **A. 连接生命周期** | [`ready`](#a-ready--渲染器就绪) | WS 连接建立后主动发送 | 发送启动齐射 |
| **B. 模型生命周期** | [`model_loaded`](#b-model_loaded--模型加载完成) | 模型加载成功 | 解析模型文件 → 配置 → 启动调度 |
| | [`model_load_failed`](#c-model_load_failed--模型加载失败) | 模型加载失败 | 记录日志 |
| **C. 动作生命周期** | [`motion_started`](#d-motion_started--动作开始) | `play_motion` 动作开始 | 可选：记录日志 |
| | [`motion_finished`](#e-motion_finished--动作结束) | 动作播放完成 | 记录日志 + 触发闲时判定 |
| **D. 用户交互** | [`hit`](#f-hit--点击命中) | 用户点击模型 HitArea | 查映射 → 发 `play_motion` 或 `play_motion_ext` |
| | [`drag_start`](#g-drag_start--拖拽开始) | 用户按下鼠标且未命中模型 | 记录拖拽状态 |
| | [`drag_end`](#h-drag_end--拖拽结束) | 用户释放鼠标且处于拖拽中 | 持久化窗口位置 |
| **E. 状态同步** | [`layout_changed`](#i-layout_changed--用户布局变更) | Shift+拖拽/滚轮改变布局 | 持久化布局参数 |
| | [`window_resized`](#j-window_resized--窗口尺寸变更) | Ctrl+滚轮缩放窗口 | 持久化窗口尺寸+位置 |
| | [`stats_state`](#k-stats_state--资源占用快照) | `get_stats` 的响应 | 更新监控 UI |
| | [`layout_state`](#l-layout_state--当前布局查询响应) | `get_layout` 的响应 | 可选：同步 UI |
| **F. 错误** | [`error`](#m-error--错误上报) | 运行时错误 | 记录日志 |

### 4.2 事件 payload 变体

`motion_finished` 事件有两种 payload 变体，**消费端必须同时处理**：

| 来源 | payload 字段 |
|:---|:---|
| `play_motion` 触发 | `group: string, index: int` |
| `play_motion_ext` 触发 | `motion_path: string` |

判定方式：检查 payload 中是否存在 `motion_path` 字段。

---

## 五、命令详细规格

### A. 初始化组

#### A.1 `load_model` — 加载模型

替换当前模型。已有模型时先卸载再加载。

- **回执**：✓ 需要 Response
- **配对事件**：成功后发送 `model_loaded`；失败时发送 `model_load_failed`

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `model_path` | string | ✓ | — | 模型短名称（如 `"Hiyori"`）。渲染器内部构造路径 `Resources/<name>/<name>.model3.json` |

**渲染器处理**：校验非空（`1001`）→ 校验路径存在（`1001`）→ 切换模型 → Response(success) → `model_loaded` 事件

**示例**：
```json
{ "type": "command", "action": "load_model", "id": "...",
  "payload": { "model_path": "Hiyori" }, "timestamp": 1710000000000 }
```

---

#### A.2 `set_position` — 设置窗口位置

- **回执**：✗ 无 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `x` | int | ✓ | `0` | 窗口左上角 X 坐标（像素，屏幕坐标系，原点屏幕左上角） |
| `y` | int | ✓ | `0` | 窗口左上角 Y 坐标（Y 轴向下） |

> 渲染器直接应用坐标，不做范围校验。负值或超屏幕坐标会导致窗口部分/完全不可见。

---

#### A.3 `set_size` — 设置窗口尺寸

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `width` | int | ✓ | — | 窗口宽度（像素，正值）。实际钳制到 100–2000 |
| `height` | int | ✓ | — | 窗口高度（像素，正值）。实际钳制到 100–2000 |

**窗口居中调整**：保持中心点不变，同步调整左上角坐标。

| error_code | 触发场景 |
|:---:|:---|
| `4004` | `width` 或 `height` 非正（≤ 0） |

---

#### A.4 `set_opacity` — 设置窗口透明度

- **回执**：✗ 无 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `opacity` | float | ✓ | `1.0` | 透明度。`0.0` = 完全透明，`1.0` = 完全不透明 |

> 不做范围校验，超出 `0.0–1.0` 的行为取决于底层窗口系统。

---

#### A.5 `set_fps` — 设置目标帧率

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `fps` | number | ✓ | — | `0` = 自适应模式（15–60fps 浮动）；`1`–`120` = 固定帧率 |

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `6003` | `fps` 为负、落入 `(0, 1)` 开区间小数、或 > 120 | `"fps must be 0 (adaptive) or 1-120"` |

---

#### A.6 `set_hit_areas` — 设置点击区域

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `hit_areas` | array&lt;string&gt; | ✓ | — | 点击区域名称数组（如 `["Head", "Body"]`）。名称需与模型 `.model3.json` 的 HitArea 定义一致 |

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `1005` | `hit_areas` 缺失或非数组 | `"hit_areas array is required"` |

---

### B. 动作与表情组

#### B.1 `play_motion` — 播放动作

播放指定动作组中的动作。

- **回执**：✗ 无 Response
- **配对事件**：开始时 `motion_started`；结束时 `motion_finished`（payload 变体 A）

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `group` | string | ✓ | `""` | 动作组名称（如 `"Idle"`、`"TapBody"`、`"TapHead"`） |
| `index` | int | 否 | `0` | 动作组内索引，从 0 开始 |
| `priority` | int | 否 | `2` | 动作优先级（见下表） |

**动作优先级常量**：

| 常量 | 值 | 用途 |
|:---|:---:|:---|
| `PriorityNone` | 0 | 无优先级 |
| `PriorityIdle` | 1 | 闲时动作（可被任何动作中断） |
| `PriorityNormal` | 2 | 普通动作（点击触发） |
| `PriorityForce` | 3 | 强制动作（不可被中断） |

> `group`/`index` 的有效值取决于当前加载的模型。控制面板需自行解析 `.model3.json` 获取合法值（`model_loaded` 事件中的 motions 始终为空数组）。`group` 不存在或 `index` 越界时**静默忽略**（不触发动作，不报错）。无模型加载时发送 `error` 事件（`2001`）。

---

#### B.2 `play_motion_ext` — 播放外部动作文件（Phase 3a ✅）

从外部绝对路径加载 `.motion3.json` 动作文件，支持淡入淡出与附带音频。

- **回执**：✓ 需要 Response
- **配对事件**：结束时 `motion_finished`（payload 变体 B，使用 `motion_path`）。**不发送 `motion_started`**。

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `motion_path` | string | ✓ | — | 动作文件绝对路径（`.motion3.json`） |
| `priority` | int | 否 | `2` | 动作优先级（同 B.1） |
| `fade_in` | float | 否 | `1.0` | 淡入时长（**秒**） |
| `fade_out` | float | 否 | `1.0` | 淡出时长（**秒**） |
| `audio_path` | string? | 否 | `""` | 附带音频文件绝对路径。引擎已初始化且文件存在则播放 |
| `lip_sync_path` | string? | 否 | — | ⚠️ 口型同步文件路径。**代码中发送，协议文档未记录**（Phase 3c 待实现） |
| `subtitle_text` | string? | 否 | — | ⚠️ 字幕文本。**代码中发送，协议文档未记录** |
| `subtitle_duration` | int64? | 否 | — | ⚠️ 字幕持续时长（毫秒）。**代码中发送，协议文档未记录** |

**渲染器处理**：校验非空（`3001`）→ 校验路径安全（`3004`）→ 校验文件存在（`3002`）→ 校验已加载模型（`2001`）→ 加载播放（`3003` 若被优先级守卫拒绝）→ Response(success)

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `3001` | `motion_path` 为空 | `"motion_path is required"` |
| `3002` | 动作文件不存在 | `"motion file not found: <path>"` |
| `3003` | 被优先级守卫拒绝或加载失败 | `"Motion rejected by priority guard or failed to load"` |
| `3004` | 路径含 `..`/`~` 等穿越字符 | `"motion_path contains unsafe traversal"` |

> ⚠️ **代码/文档偏差**：`lip_sync_path`、`subtitle_text`、`subtitle_duration` 三个字段在 Java 参考实现（`MountedBehaviorEngine.java`）中已构造并发送，但 [commands.md §10.1](./commands.md#101-play_motion_ext--播放外部动作文件phase-3a-已实现) 未记录。渲染器当前接受这些字段，但 lipSync 功能待 Phase 3c 实现。

---

#### B.3 `stop_motion` — 停止动作

停止当前所有正在播放的动作。无动作时静默忽略。

- **回执**：✗ 无 Response
- **Payload**：`{}`

---

#### B.4 `set_expression` — 设置表情

- **回执**：✗ 无 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `expression_id` | string | ✓ | `""` | 表情 ID（如 `"default"`，来源于 `.model3.json` 的 Expressions 定义） |

> `expression_id` 不存在时**静默忽略**。

---

### C. 音频组（Phase 3b ✅）

#### C.1 `play_audio` — 播放音频（Phase 3b ✅）

播放独立音频文件（OGG）。**即发即忘**——播放完成不上报事件。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `audio_path` | string | ✓ | — | 音频文件绝对路径 |
| `volume` | float | 否 | `1.0` | 本次播放音量（`0.0`–`1.0`） |

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `7001` | `audio_path` 为空 | `"audio_path is required"` |
| `7002` | `AudioManager` 未初始化 | `"Audio engine not initialized"` |
| `7003` | 音频文件不存在 | `"audio file not found: <path>"` |
| `7004` | 路径含不安全字符 | `"audio_path contains unsafe traversal"` |

---

#### C.2 `stop_audio` — 停止音频（Phase 3b ✅）

停止所有正在播放的音频。

- **回执**：✓ 需要 Response
- **Payload**：`{}`
- 无论引擎是否初始化，均返回 `success: true`

---

#### C.3 `set_volume` — 设置音量/静音（Phase 3b ✅）

设置全局音量和/或静音状态。两个字段均为可选，按需传入。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `volume` | float? | 否 | `1.0` | 播放音量（`0.0`–`1.0`）。字段存在时设置音量 |
| `muted` | bool? | 否 | `false` | 是否静音。字段存在时设置静音状态 |

> 字段不存在时对应属性保持不变。

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `7002` | `AudioManager` 未初始化 | `"Audio engine not initialized"` |

---

### D. 字幕组

#### D.1 `show_subtitle` — 显示字幕

在渲染窗口叠加显示一条字幕，支持自动消失或常驻。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `text` | string | ✓ | — | 字幕文本。缺失返回 `10001` |
| `duration` | int64 | 否 | `0` | 显示时长（**毫秒**）。`0` = 不自动隐藏，需 `hide_subtitle` 清除 |
| `font_name` | string? | 否 | `"Microsoft YaHei"` | 字体名称 |
| `font_size` | double? | 否 | `48.0` | 字号（ASS points） |
| `primary_color` | uint32? | 否 | `0x00FFFFFF` | 文字颜色（AABBGGRR，见 [§1.3](#13-颜色字段格式)） |
| `outline_color` | uint32? | 否 | `0x00111111` | 描边颜色 |
| `outline_width` | double? | 否 | `1.8` | 描边宽度 |
| `shadow_color` | uint32? | 否 | `0x00000000` | 阴影颜色 |
| `shadow_depth` | double? | 否 | `0.0` | 阴影深度（`0.0` = 无阴影） |
| `alignment` | int? | 否 | `2` | ASS 数字键盘对齐（`1`=左下 … `2`=中下 … `9`=右上） |
| `margin_v` | double? | 否 | `30.0` | 垂直边距（像素） |
| `edge_blur` | double? | 否 | `0.6` | ⚠️ 边缘模糊（ASS `\blur`）。代码中已实现，文档未记录 |
| `font_weight` | int? | 否 | `-1` | ⚠️ 字重（`-1`=不注入，`0`=normal，`1`=bold）。代码中已实现，文档未记录 |
| `letter_spacing` | double? | 否 | `0.5` | ⚠️ 字间距（ASS `\fsp`）。代码中已实现，文档未记录 |
| `bg_box_enabled` | bool? | 否 | `false` | ⚠️ 是否启用背景框。代码中已实现，文档未记录 |
| `bg_box_color` | uint32? | 否 | `0x80000000` | ⚠️ 背景框颜色（50% 透明黑）。代码中已实现，文档未记录 |
| `bg_box_padding_x` | double? | 否 | `12.0` | ⚠️ 背景框水平内边距。代码中已实现，文档未记录 |
| `bg_box_padding_y` | double? | 否 | `6.0` | ⚠️ 背景框垂直内边距。代码中已实现，文档未记录 |

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `10001` | `text` 为空或缺失 | `"text is required"` |
| `10002` | 字幕引擎未初始化 | `"Subtitle engine not initialized"` |

> ⚠️ **代码/文档偏差**：`margin_v`、`edge_blur`、`font_weight`、`letter_spacing`、`bg_box_enabled`、`bg_box_color`、`bg_box_padding_x`、`bg_box_padding_y` 这 8 个字段在 Java 参考实现（`Protocol.java` + `SubtitleStyle.java`）中已构造并发送，但 [commands.md §18](./commands.md#18-show_subtitle--显示字幕) 仅记录了前 10 个基础样式字段。
>
> **v1 限制**：不发送 `subtitle_shown`/`subtitle_hidden` 事件。控制面板若需知道字幕何时消失，应自行用 `duration` 计时。

---

#### D.2 `hide_subtitle` — 隐藏字幕

立即清除所有字幕。无字幕时静默成功。

- **回执**：✓ 需要 Response
- **Payload**：`{}`
- 无论引擎状态如何，均返回 `success: true`

---

#### D.3 `set_subtitle_style` — 设置字幕默认样式

更新默认样式参数（写入 ASS 轨道 style 0）。后续 `show_subtitle` 若不携带样式覆盖字段，则使用此默认样式。

- **回执**：✓ 需要 Response
- **Payload 字段**：同 [D.1 `show_subtitle`](#d1-show_subtitle--显示字幕) 的全部样式字段（不含 `text`/`duration`）。所有字段均可选，仅传入的字段更新，未传入字段保持当前值。

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `10002` | 字幕引擎未初始化 | `"Subtitle engine not initialized"` |

---

#### D.4 `set_subtitle_adjust_mode` — 字幕调整模式

进入/退出字幕调整模式（启用/关闭字幕区域边框预览）。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `enabled` | bool | ✓ | — | `true` = 进入调整模式（启用边框预览），`false` = 退出 |

> 本指令恒成功，无错误码。

---

#### D.5 `set_subtitle_layout` — 设置字幕布局

设置字幕位置偏移、渲染区域与字号。即使字幕引擎未初始化也接受——参数被缓存待后续生效。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `offset_x` | double | ✓ | — | 字幕水平偏移（像素） |
| `offset_y` | double | ✓ | — | 字幕垂直偏移（像素） |
| `area_width` | int | ✓ | — | 字幕区域宽度（像素，`0` = 自动） |
| `area_height` | int | ✓ | — | 字幕区域高度（像素，`0` = 自动） |
| `font_size` | double | ✓ | — | 字号（ASS points） |

> 本指令恒成功，无错误码（即使引擎未初始化也缓存参数并返回成功）。

---

### E. 布局组

#### E.1 `set_layout` — 设置用户布局

设置模型用户布局偏移与缩放（手动位置/缩放调整）。与 stub 的 `set_scale` 不同，此指令**实际生效**。

- **回执**：✓ 需要 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `offset_x` | float? | 否 | 当前值 | 水平偏移量 |
| `offset_y` | float? | 否 | 当前值 | 垂直偏移量 |
| `scale` | float? | 否 | 当前值 | 缩放比例 |

> 三字段均可选，仅传入字段更新对应属性，未传入字段保持当前值。

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `8001` | 当前无模型加载 | `"No model loaded"` |

---

#### E.2 `get_layout` — 查询用户布局

查询当前模型布局参数。**数据通过 `layout_state` 事件回传**，不走 Response。

- **回执**：✗ 无 Response（响应走 [`layout_state`](#l-layout_state--当前布局查询响应) 事件）
- **Payload**：`{}`

> ⚠️ **不发送 Response**：因 `createResponse` 的 payload 固定为 `{}`，无法承载布局数据。控制面板应注册 `layout_state` 事件处理器接收数据，**不应**为 `get_layout` 挂 pending request。

| error_code | 触发场景 | error_message |
|:---:|:---|:---|
| `8001` | 当前无模型加载 | `"No model loaded"` |

---

#### E.3 `reset_layout` — 重置用户布局

重置为默认值（`offset_x`/`offset_y`/`scale` 恢复初始）。

- **回执**：✓ 需要 Response
- **Payload**：`{}`
- 恒返回 `success: true`

---

### F. 监控组

#### F.1 `get_stats` — 请求资源占用快照

请求渲染器上报资源占用快照。控制面板以固定周期（默认 2 秒）轮询。**数据通过 `stats_state` 事件回传**。

- **回执**：✗ 无 Response（响应走 [`stats_state`](#k-stats_state--资源占用快照) 事件）
- **Payload**：`{}`

> ⚠️ 同 E.2 的"走事件绕过空 payload Response"模式。控制面板应注册 `stats_state` 事件处理器，不应为 `get_stats` 挂 pending request（10 秒超时后会落空）。

---

### G. 生命周期组

#### G.1 `shutdown` — 优雅关闭

请求渲染器优雅关闭。

- **回执**：✓ 需要 Response
- **Payload**：`{}`
- 渲染器先发送 Response(success)，然后触发主循环退出。控制面板收到 Response 后应预期 WS 连接即将关闭。

---

### H. 保留/Stub

#### H.1 `hello` — 握手（保留）

控制面板向渲染器的握手消息。**当前流程不主动发送**（等待 `ready` 事件后直接 `load_model`），保留用于未来版本协商。

- **回执**：✗ 无 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `client_name` | string? | 否 | `"unknown"` | 客户端标识 |
| `version` | string? | 否 | — | 协议版本 |

---

#### H.2 `set_scale` — 设置缩放（⚠️ STUB）

- **回执**：✗ 无 Response

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `scale` | float | ✓ | `1.0` | 缩放比例 |

> ⚠️ **当前为 stub**：渲染器收到后仅记录日志（`"set_scale: <value> (not fully implemented)"`），**不产生实际效果**。模型缩放请改用 [`set_layout`](#e1-set_layout--设置用户布局) 的 `scale` 字段。

---

## 六、事件详细规格

### A. `ready` — 渲染器就绪

WS 连接建立后渲染器**主动发送**，通知控制面板可以下发指令。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `version` | string | 渲染器协议版本（当前 `"1.0.0"`） |
| `capabilities` | array&lt;string&gt; | 渲染器支持的能力列表（当前 `["live2d"]`） |

**控制面板必须的行为**：发送启动齐射（`load_model` → `set_position` → `set_size` → `set_opacity` → `set_fps` → `set_volume` → `set_layout` → `set_subtitle_layout` → `set_subtitle_style`）。

**`ready` 之前发送的 command 不保证被处理。**

---

### B. `model_loaded` — 模型加载完成

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `model_id` | string | 模型短名称（与 `load_model` 的 `model_path` 一致） |
| `motions` | array | ⚠️ **始终为空数组 `[]`**。控制面板需自行解析 `.model3.json` |
| `expressions` | array | ⚠️ **始终为空数组 `[]`**。同上 |

**模型能力发现**（因 motions/expressions 始终为空，必须自行解析）：

| 需要的数据 | 提取方式（`.model3.json`） | 用途 |
|:---|:---|:---|
| 动作组名称 | `FileReferences.Motions` 的所有 key | `play_motion` 的 `group` |
| 每组动作数 | 每个 key 对应数组的长度 | `play_motion` 的 `index` 范围 |
| 表情 ID | `FileReferences.Expressions[*].Name` | `set_expression` 的 `expression_id` |
| HitArea 名 | `HitAreas[*].Name` | `set_hit_areas` 的 `hit_areas` |

---

### C. `model_load_failed` — 模型加载失败

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `error_code` | int | 错误码（通常 `1001`） |
| `error_message` | string | 错误描述 |

---

### D. `motion_started` — 动作开始

由 `play_motion` 触发。`play_motion_ext` **不发送**此事件。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `group` | string | 动作组名称 |
| `index` | int | 动作组内索引 |

> 可选处理：UI 状态展示或日志记录。

---

### E. `motion_finished` — 动作结束

⚠️ **两种 payload 变体**，消费端必须同时处理：

**变体 A**（`play_motion` 触发）：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `group` | string | 动作组名称 |
| `index` | int | 动作组内索引 |

**变体 B**（`play_motion_ext` 触发）：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `motion_path` | string | 动作文件绝对路径（原始 `play_motion_ext` 传入的路径） |

**判定方式**：检查 payload 是否含 `motion_path` 字段。

**控制面板建议行为**：记录日志；若为非闲时动作（`group` 不等于 `"Idle"`，或 `motion_path` 不属于 idle 组），触发闲时判定。

---

### F. `hit` — 点击命中

用户点击模型 HitArea 时发送。仅点击**命中模型区域**时触发（未命中则进入拖拽）。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `area_id` | string | 命中区域标识。当前固定 `"head"` 或 `"body"`（**小写**） |
| `x` | float | 鼠标 X 坐标（窗口客户区坐标） |
| `y` | float | 鼠标 Y 坐标 |
| `button` | int | 鼠标按键。`0` = 左键 |

**控制面板建议行为**：
1. 若已挂载语音包且定义了 `area_id` 对应的 group → 走 `play_motion_ext`
2. 否则查模型配置 HitArea 映射 → 发 `play_motion`
3. 默认映射：`head → TapHead`、`body → TapBody`（priority=2）
4. HitArea 名称大小写容错（渲染器发小写，配置可能用 PascalCase）

---

### G. `drag_start` — 拖拽开始

用户按下鼠标且**未命中**模型 HitArea 时触发。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 拖拽起始鼠标 X 坐标（窗口客户区） |
| `y` | float | 拖拽起始鼠标 Y 坐标 |

---

### H. `drag_end` — 拖拽结束

用户释放鼠标且之前处于拖拽状态时触发。**仅在收到过 `drag_start` 的情况下处理**。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 释放时鼠标 X 坐标（窗口客户区） |
| `y` | float | 释放时鼠标 Y 坐标 |
| `window_x` | int | 窗口最终左上角 X 坐标（**屏幕像素坐标**） |
| `window_y` | int | 窗口最终左上角 Y 坐标 |

**控制面板必须的行为**：提取 `window_x`/`window_y` → 更新实例状态 → 持久化到配置文件（下次启动时通过 `set_position` 恢复）。

---

### I. `layout_changed` — 用户布局变更

用户通过交互（Shift+拖拽模型、Shift+滚轮缩放）改变布局后发送。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `offset_x` | float | 水平偏移量 |
| `offset_y` | float | 垂直偏移量 |
| `scale` | float | 缩放比例 |

**控制面板建议行为**：持久化（下次启动通过 `set_layout` 恢复）。

> 与 `set_layout` 指令形成**双向同步**——控制面板可通过指令设置布局，用户交互变更也通过此事件回传。

---

### J. `window_resized` — 窗口尺寸变更

用户通过交互（Ctrl+滚轮）缩放窗口后发送。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `window_width` | int | 新窗口宽度（像素） |
| `window_height` | int | 新窗口高度（像素） |
| `window_x` | int | 新窗口左上角 X 坐标（屏幕坐标系，保持中心点不变） |
| `window_y` | int | 新窗口左上角 Y 坐标 |

**控制面板建议行为**：持久化（下次启动通过 `set_size` + `set_position` 恢复）。

> 窗口居中缩放：渲染器调整尺寸时保持中心点不变（同步调整左上角坐标），因此 payload 同时包含尺寸和位置。

---

### K. `stats_state` — 资源占用快照

作为 [`get_stats`](#f1-get_stats--请求资源占用快照) 命令的响应事件发送（每收到一条 `get_stats` 即 emit 一条 `stats_state`）。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `cpu_percent` | float | 渲染器进程 CPU 利用率（`0.0`–`100.0+`，多线程可 >100）。**始终可用** |
| `rss_bytes` | int64 | 渲染器进程常驻内存（RSS），字节数。**始终可用** |
| `gpu_percent` | float? | GPU 利用率百分比。Windows 经 PDH 采集；Linux Stub 或失败时为 `null` |
| `gpu_name` | string? | GPU 适配器名称（如 `"NVIDIA GeForce RTX 3060"`）。不可用时 `null` |
| `vram_used_bytes` | int64? | 已用显存，字节数。Windows 经 DXGI per-process 采集；不可用时 `null` |
| `vram_total_bytes` | int64? | 显存总量，字节数。不可用时 `null` |
| `timestamp_ms` | int64 | 渲染器侧采集时间戳（Unix 毫秒）。**始终可用** |

**平台差异**：
- **Windows**：经 PDH（GPU%）+ DXGI（VRAM，per-process `QueryVideoMemoryInfo`）采集，全字段可用
- **Linux**：GPU 监视器为 Stub（`IGpuMonitor::initialize()` 返回 false），4 个 GPU 相关字段恒为 `null`
- **Vulkan 变体**：VRAM 数值依赖 WDDM 驱动报告，可能偏低或为 0（已知限制，非回归）

> **`id` 不用于匹配**：`stats_state` 是事件而非 Response，其 `id` 由渲染器新建，不复用 `get_stats` 的 `id`。按 `action` 路由。

---

### L. `layout_state` — 当前布局查询响应

作为 [`get_layout`](#e2-get_layout--查询用户布局) 命令的响应事件发送。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `offset_x` | float | 水平偏移量 |
| `offset_y` | float | 垂直偏移量 |
| `scale` | float | 缩放比例 |

> 与 `layout_changed` 的区别：`layout_changed` 由用户交互触发（被动通知），`layout_state` 由控制面板主动查询触发（请求-响应）。两者 payload 结构相同但语义不同。同 `stats_state`，`id` 不复用 `get_layout` 的 `id`。

---

### M. `error` — 错误上报

渲染器运行时错误（非命令级错误，命令级错误走 Response）。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `error_code` | int | 错误码（见 [error-codes.md](./error-codes.md)） |
| `error_message` | string | 错误描述 |

---

## 七、交互模式

协议中存在三种交互模式，新实现必须理解它们的区别：

### 7.1 模式一：Fire-and-forget（即发即忘）

控制面板发送命令后**不等待任何回执**，继续后续操作。

**适用命令**：`set_position`、`set_opacity`、`play_motion`、`stop_motion`、`set_expression`、`set_scale`、`hello`

```
控制面板 ──command──► 渲染器（无 Response）
```

**实现建议**：直接发送，不挂 pending request。状态确认依赖后续事件流（如 `motion_finished`）。

---

### 7.2 模式二：Request-Response

控制面板发送命令后等待 Response（按 `id` 匹配），建议 10 秒超时。

**适用命令**：`load_model`、`set_size`、`set_fps`、`set_hit_areas`、`play_motion_ext`、`play_audio`、`stop_audio`、`set_volume`、`set_layout`、`reset_layout`、`show_subtitle`、`hide_subtitle`、`set_subtitle_style`、`set_subtitle_adjust_mode`、`set_subtitle_layout`、`shutdown`

```
控制面板 ──command──► 渲染器
控制面板 ◄─response── 渲染器（success/error_code/error_message 在顶层，payload 为 {}）
```

**实现建议**：
- 维护 `Map<id, CompletableFuture/Promise>` pending request 表
- 收到 Response 时按 `id` 查找并完成
- 设置 10 秒超时，超时后清理 entry
- ⚠️ **参考实现说明**：Java 参考实现定义了此机制（`MessageDispatcher.expectResponse()`）但**当前生产代码未实际调用**——所有命令实际为 fire-and-forget，依赖事件流确认状态。新实现可选择严格 request-response 或保持事件驱动。

---

### 7.3 模式三：Command → Event（伪响应）

控制面板发送命令后，**不通过 Response 而通过独立事件**接收数据。原因：`createResponse` 的 payload 固定为 `{}`，无法承载结构化数据。

**适用配对**：

| 命令 | 响应事件 | 数据内容 |
|:---|:---|:---|
| `get_stats` | `stats_state` | CPU/内存/GPU/显存快照 |
| `get_layout` | `layout_state` | 布局参数 |

```
控制面板 ──command(id=A)──► 渲染器
控制面板 ◄──event(id=B)──── 渲染器（id=B，不复用 A）
```

**实现建议（关键）**：
- ⚠️ **按 `action` 路由，绝不按 `id` 匹配**
- 事件的 `id` 由渲染器新建，与命令的 `id` 无关
- 不要为这些命令挂 pending request（10 秒后会落空）

---

### 7.4 模式四：主动通知事件

渲染器主动发送的事件，控制面板不发起请求。

**适用事件**：`ready`、`model_loaded`、`model_load_failed`、`motion_started`、`motion_finished`、`hit`、`drag_start`、`drag_end`、`layout_changed`、`window_resized`、`error`

```
控制面板 ◄──event── 渲染器（主动触发）
```

---

## 八、错误码索引

错误码按模块分段，每段预留 100 个用于扩展。**完整定义见 [error-codes.md](./error-codes.md)**。

| 段 | 模块 | 范围 | 关键错误码 |
|:---:|:---|:---|:---|
| 1000 | 模型相关 | 1000–1099 | `1001`（模型路径无效）、`1005`（hit_areas 缺失/非数组） |
| 2000 | 动画相关 | 2000–2099 | `2001`（无模型加载） |
| 3000 | 外部动作（Phase 3a） | 3000–3099 | `3001`（motion_path 为空）、`3002`（文件不存在）、`3003`（优先级拒绝）、`3004`（路径不安全） |
| 4000 | 窗口相关 | 4000–4099 | `4004`（width/height 非正） |
| 5000 | 通信相关 | 5000–5099 | `5003`（未知 action） |
| 6000 | 系统/通用 | 6000–6099 | `6001`（handler 异常）、`6003`（fps 非法） |
| 7000 | 音频（Phase 3b） | 7000–7099 | `7001`（audio_path 为空）、`7002`（引擎未初始化）、`7003`（文件不存在）、`7004`（路径不安全） |
| 8000 | 布局相关 | 8000–8099 | `8001`（无模型加载） |
| 9000 | 资源监视 | 9000–9099 | `9001`（采集失败，预留） |
| 10000 | 字幕相关 | 10000–10099 | `10001`（text 为空）、`10002`（引擎未初始化） |

### 未知 action 的处理

渲染器收到未注册的 action 时，返回 Response（`success: false`，`error_code: 5003`，`error_message: "Unknown action: <action>"`），`id` 复用原始 Command 的 `id`。

---

## 九、已知文档/代码偏差汇总

> 以下偏差已在本文档中就地标注（⚠️ 标记）。此处集中汇总，供实现者参考。权威以代码为准。

| # | 位置 | 偏差描述 | 实际情况（以代码为准） |
|:---:|:---|:---|:---|
| 1 | `play_motion_ext` | [commands.md §10.1](./commands.md#101-play_motion_ext--播放外部动作文件phase-3a-已实现) 未记录 `lip_sync_path`/`subtitle_text`/`subtitle_duration` | `MountedBehaviorEngine.java` 中已构造并发送这三个字段 |
| 2 | `show_subtitle` / `set_subtitle_style` | [commands.md §18/§20](./commands.md#18-show_subtitle--显示字幕) 仅记录 10 个样式字段 | `Protocol.java` + `SubtitleStyle.java` 实际发送 18 个样式字段（多出 `margin_v`、`edge_blur`、`font_weight`、`letter_spacing`、`bg_box_*` 等 8 个） |
| 3 | 颜色格式 | 部分文档描述为 "RRGGBBTT" | 代码默认值（如 `0x00FFFFFF` = 不透明白）表明实际格式为 **AABBGGRR**（与 ASS 一致）。本文档以 AABBGGRR 为准 |
| 4 | `set_scale` | 协议中存在此命令 | 渲染器侧为 **stub**，仅记日志不生效。应使用 `set_layout` 的 `scale` 字段 |
| 5 | `model_loaded` 的 `motions`/`expressions` | 字段存在但始终为空 `[]` | 控制面板必须自行解析 `.model3.json` |
| 6 | request-response 机制 | Java 参考实现定义了 `CompletableFuture` 关联 | **当前生产代码未调用 `expectResponse`**，所有命令实际为 fire-and-forget |

---

## 十、消息时序约定

### 10.1 连接初始化时序

```
控制面板                                       渲染器
   │                                            │
   │  启动 WS Server，监听 127.0.0.1:9001        │
   │                                            │
   │  注册令牌，启动渲染器子进程                   │
   │ ────────────────────────────────────────►  │ (进程创建)
   │                                            │
   │           ws://127.0.0.1:9001/?instance_id=N&token=<hex>
   │ ◄──────────── TCP + WS 升级 ──────────────  │
   │  [Origin 守卫 → instance_id 校验 → token 校验] │
   │                                            │
   │ ◄───── event: ready ─────────────────────── │
   │       {version:"1.0.0", capabilities:["live2d"]} │
   │                                            │
   │  收到 ready → 发送 9 条启动齐射：             │
   │ ── load_model ───────────────────────────►  │
   │ ── set_position ─────────────────────────►  │
   │ ── set_size ─────────────────────────────►  │
   │ ── set_opacity ──────────────────────────►  │
   │ ── set_fps ──────────────────────────────►  │
   │ ── set_volume ───────────────────────────►  │
   │ ── set_layout（仅非默认时）────────────────► │
   │ ── set_subtitle_layout ─────────────────►  │
   │ ── set_subtitle_style ───────────────────►  │
   │                                            │
   │ ◄───── event: model_loaded ──────────────── │
   │ ── set_hit_areas ────────────────────────►  │
   │                                            │
   │ ═══════ 稳态运行 ═══════                    │
```

### 10.2 并发与顺序

| 规则 | 说明 |
|:---|:---|
| **可并发发送** | 控制面板可并发发送多个 command，无需等待前一个 Response |
| **Response 不依赖顺序** | 通过 `id` 匹配，不依赖到达顺序 |
| **事件可能交叉** | Event 可能在 command 和其 Response 之间到达 |
| **同 action 按序处理** | 渲染器对同一 action 的多个 command 按接收顺序处理，不合并/去重 |

### 10.3 渲染器处理节奏

- 每渲染帧最多处理 **50 条**入站消息
- 超出部分在下一帧处理（队列缓冲，上限 **1000 条**）
- 短时间大量发送不会丢失，但处理有延迟

### 10.4 ready 超时

控制面板在 WS 连接建立后 **10 秒内**未收到 `ready` 事件，建议：
1. 关闭当前连接
2. 判定渲染器异常，触发重启或报告错误

---

## 附录：协议常量速查

### 连接参数

| 参数 | 值 |
|:---|:---|
| 协议 | `ws://`（无 TLS） |
| 地址 | `127.0.0.1`（本地回环） |
| 端口 | `9001` |
| 帧类型 | Text Frame（UTF-8） |
| Ping 间隔 | 45 秒（渲染器发起） |
| 最大重连间隔 | 30 秒（渲染器侧，指数退避） |
| 连接数 | 每实例一条 |
| 消息队列上限 | 1000 条（渲染器侧） |
| 每帧处理上限 | 50 条（渲染器侧） |

### 动作优先级

| 常量 | 值 |
|:---|:---:|
| PriorityNone | 0 |
| PriorityIdle | 1 |
| PriorityNormal | 2 |
| PriorityForce | 3 |

### WS 关闭码（控制面板侧自定义）

| 关闭码 | 场景 |
|:---:|:---|
| `1000` | 正常关闭（含"被新连接替换"） |
| `4000` | 缺少 `instance_id` 查询参数 |
| `4001` | Origin 被拒（浏览器连接） |
| `4002` | 令牌不匹配 |

---

> **文档版本**：v1.0 · 基于渲染器协议 v1.0.0（commit `12cef2d`）。
> 如协议发生变更（新增命令/事件、调整字段类型、改变交互模式），本文档需同步更新。
