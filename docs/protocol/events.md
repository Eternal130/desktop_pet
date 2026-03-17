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

## 10. Phase 3 事件（待实现）

| action | payload | 说明 |
|:---|:---|:---|
| `audio_started` | `{ audio_path }` | 音频开始播放 |
| `audio_ended` | `{ audio_path }` | 音频播放结束 |
