# 控制面板 → 渲染器（Commands）

> 协议概述与 Envelope 格式参见 [通信协议](./README.md)。
> 渲染器→控制面板事件参见 [Events](./events.md)。

---

## 1. `load_model` — 加载模型

替换当前模型。已有模型时自动卸载旧模型再加载新模型。**需要 Response 回执**。

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
2. 使用 `stat()` 验证 `model_path` 路径是否存在，否则返回错误 `1001`
3. 调用 `LAppLive2DManager::ChangeScene(modelPath)` 执行模型切换
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

播放指定动作组中的动作。无 Response 回执。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `group` | string | ✓ | `""` | 动作组名称（如 `"Idle"`、`"TapBody"`、`"TapHead"`） |
| `index` | int | 否 | `0` | 动作组内索引。`0` 表示第一个动作 |
| `priority` | int | 否 | `2` | 动作优先级（见下方优先级常量） |

**动作优先级常量**（定义于 `LAppDefine.cpp`）：

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

**渲染器处理**：调用 `LAppModel::StartMotionWithCustomData(group, index, priority, callback, ctx)`。动作播放开始时发送 `motion_started` 事件，播放结束时通过回调发送 `motion_finished` 事件。若无模型加载，发送 `error` 事件（错误码 `2001`）。

---

## 3. `stop_motion` — 停止动作

停止当前正在播放的所有动作。

**Payload**：空对象 `{}`

---

## 4. `set_expression` — 设置表情

切换模型表情。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `expression_id` | string | ✓ | `""` | 表情 ID（如 `"default"`，来源于 `.model3.json` 的 Expressions 定义） |

---

## 5. `set_position` — 设置窗口位置

设置渲染器窗口在屏幕上的位置。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `x` | int | ✓ | `0` | 窗口左上角 X 坐标（像素，屏幕坐标系） |
| `y` | int | ✓ | `0` | 窗口左上角 Y 坐标（像素） |

**渲染器处理**：调用 `glfwSetWindowPos(window, x, y)`。

---

## 6. `set_scale` — 设置模型缩放

设置模型的显示缩放比例。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `scale` | float | ✓ | `1.0` | 缩放比例 |

> ⚠️ **当前状态**：该指令已注册但**未完整实现**，收到后仅记录日志。

---

## 7. `set_opacity` — 设置窗口透明度

设置渲染器窗口的整体透明度。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `opacity` | float | ✓ | `1.0` | 透明度。`0.0` = 完全透明，`1.0` = 完全不透明 |

**渲染器处理**：调用 `glfwSetWindowOpacity(window, opacity)`。

---

## 8. `hello` — 握手

控制面板向渲染器发送的握手消息。当前实现中仅记录日志。

**Payload**：

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|:---|:---|:---:|:---|:---|
| `client_name` | string | 否 | `"unknown"` | 客户端标识 |
| `version` | string | 否 | — | 协议版本 |

> **说明**：当前握手流程中控制面板不主动发送 `hello`，而是等待渲染器的 `ready` 事件后直接发送 `load_model`。`hello` 指令保留用于未来版本协商。

---

## 9. `shutdown` — 优雅关闭

请求渲染器优雅关闭。**需要 Response 回执**。

**Payload**：空对象 `{}`

**渲染器处理**：先发送 Response（`success: true`），然后调用 `glfwSetWindowShouldClose(window, GLFW_TRUE)` 触发主循环退出。

---

## 10. Phase 3 指令（待实现）

以下指令已在协议文档中定义，但尚未实现。

| action | payload | 需要回执 | 说明 |
|:---|:---|:---:|:---|
| `set_audio_mapping` | `{ mappings: [{ motion_group, audio_path }] }` | **是** | 下发音频映射配置 |
| `set_volume` | `{ volume: 0.0-1.0 }` | 否 | 设置音量 |
| `set_mute` | `{ muted: bool }` | 否 | 静音/取消静音 |
| `play_audio` | `{ audio_path }` | 否 | 播放独立音频 |
| `stop_audio` | `{}` | 否 | 停止音频 |
