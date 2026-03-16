# 渲染器 → 控制面板（Events）

> 协议概述与 Envelope 格式参见 [通信协议](./README.md)。
> 控制面板→渲染器指令参见 [Commands](./commands.md)。

---

## 1. `ready` — 渲染器就绪

渲染器 WebSocket 连接建立后主动发送，通知控制面板可以开始下发指令。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `version` | string | 渲染器版本 |
| `capabilities` | object | 渲染器能力描述 |

**控制面板处理**（`AppOrchestrator`）：
1. `stateManager.setConnected(true)`
2. 发送 `load_model`（使用 `config.model().currentModelName()`）
3. `expectResponse()` 等待回执（10 秒超时）
4. 发送 `set_position`（使用 `config.window().positionX/Y()`）
5. `flushPendingCommands()` 重放断连期间缓存的关键指令

---

## 2. `model_loaded` — 模型加载完成

模型成功加载后渲染器发送。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `model_id` | string | 模型短名称（与 `load_model` 的 `model_path` 一致） |
| `motions` | array | 动作组列表。⚠️ **当前始终为空数组 `[]`** |
| `expressions` | array | 表情列表。⚠️ **当前始终为空数组 `[]`** |

> **重要**：由于 C++ 端未实现动作/表情枚举填充，Java 端需通过 `ModelInfoParser` 自行解析 `Resources/<model>/<model>.model3.json` 获取动作组（`motionGroups: Map<String, Integer>`）和表情列表。

**示例**：

```json
{
  "type": "event", "action": "model_loaded",
  "id": "...", "payload": { "model_id": "Hiyori", "motions": [], "expressions": [] },
  "timestamp": ...
}
```

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

---

## 5. `motion_finished` — 动作结束

动作播放完成后通过 `CubismMotion` 的 `FinishedMotionCallback` 回调发送。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `group` | string | 动作组名称 |
| `index` | int | 动作组内索引 |

**实现细节**：使用 `MotionFinishedCtx` 结构体通过 `SetFinishedMotionCustomData()` 传递上下文（group、index、emitter），回调触发后 `delete ctx` 释放内存。

---

## 6. `hit` — 点击命中

用户点击模型 HitArea 时渲染器发送。仅在点击命中模型区域时触发（未命中时触发拖拽）。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `area_id` | string | 命中区域标识。当前支持：`"head"`、`"body"`（**小写**） |
| `x` | float | 鼠标 X 坐标（窗口客户区坐标） |
| `y` | float | 鼠标 Y 坐标（窗口客户区坐标） |
| `button` | int | 鼠标按键。`0` = 左键（GLFW `GLFW_MOUSE_BUTTON_LEFT`） |

**HitArea 判定逻辑**（`LAppDelegate.cpp`）：
1. 鼠标按下时，将屏幕坐标转换为模型本地坐标
2. 调用 `IsHitModel(x, y)` 判断是否命中模型区域
3. 命中 → 进一步判断：`model->HitTest("Head", x, y)` 为 `true` → `area_id = "head"`，否则 → `area_id = "body"`
4. 未命中 → 进入拖拽模式（不发送 `hit` 事件）

**控制面板处理**（`InteractionHandler`）：
- 根据 `area_id` 查找 `ModelConfig` 中的映射（PascalCase key，如 `"Head"` → `TapHead`）
- 内置默认映射：`head → TapHead`、`body → TapBody`
- 构建 `play_motion` command（`group` = 映射结果，`index` = 0，`priority` = 映射优先级或默认 2）

---

## 7. `drag_start` — 拖拽开始

用户按下鼠标且未命中模型 HitArea 时触发。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 拖拽起始鼠标 X 坐标（窗口客户区） |
| `y` | float | 拖拽起始鼠标 Y 坐标 |

**控制面板处理**：`isDragging.set(true)`，作为 `drag_end` 的前置条件。

---

## 8. `drag_end` — 拖拽结束

用户释放鼠标且之前处于拖拽状态时触发。

**Payload**：

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `x` | float | 释放时鼠标 X 坐标（窗口客户区） |
| `y` | float | 释放时鼠标 Y 坐标 |
| `window_x` | int | 窗口最终左上角 X 坐标（**屏幕像素坐标**，通过 `glfwGetWindowPos` 获取） |
| `window_y` | int | 窗口最终左上角 Y 坐标 |

**拖拽状态防护**：
- 渲染器端：先捕获 `wasDragging = _isDragging`，再置 `_isDragging = false`，仅在 `wasDragging == true` 时发送事件
- 控制面板端：仅在 `isDragging.getAndSet(false)` 返回 `true` 时处理（需有对应的 `drag_start`）

**控制面板处理**：提取 `window_x`/`window_y` → 更新 `PetStateManager` → 更新 `PetConfig` → `ConfigManager.save()`（持久化到 `config.json`）。

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
