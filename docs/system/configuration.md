# 配置文件设计

> 系统设计概述参见 [系统设计](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、MVP 阶段：硬编码配置

MVP 阶段不使用配置文件，所有参数硬编码在源码中：

| 参数 | 硬编码值 | 说明 |
|:---|:---|:---|
| 模型路径 | 源码常量 | 指向 SDK 示例模型（如 `Haru`） |
| 窗口初始位置 | 屏幕右下角 | 启动时计算 |
| 窗口透明度 | 1.0 | 完全不透明模型，透明背景 |
| 模型缩放 | 1.0 | 默认缩放 |
| 拖拽模式 | "direct" | 直接跟随 |
| 闲时动作间隔 | 10 秒 | 定时随机触发 |
| HitArea→动作映射 | 硬编码 | Head→TapHead, Body→TapBody 等 |

---

## 二、Phase 2：用户配置 (config.json)

> **Phase 2 实现**：引入控制面板后，支持 JSON 配置文件读写和用户界面修改。音频相关配置在 Phase 3 启用。

存储路径：`~/.config/desktop-pet/config.json`

```json
{
  "window": {
    "position_x": 1200,
    "position_y": 600,
    "opacity": 1.0
  },
  "model": {
    "current_model_path": "models/haru/haru.model3.json",
    "scale": 1.0
  },
  "audio": {
    "volume": 0.8,
    "muted": false,
    "audio_dir": "audio/"
  },
  "behavior": {
    "drag_mode": "direct",
    "idle_interval_seconds": 10
  },
  "system": {
    "auto_start": false
  }
}
```

| 字段 | 类型 | 默认值 | 说明 | 阶段 |
|:---|:---|:---|:---|:---:|
| `window.position_x` | int | 屏幕右下角 | 窗口 X 坐标 | Phase 2 |
| `window.position_y` | int | 屏幕右下角 | 窗口 Y 坐标 | Phase 2 |
| `window.opacity` | float | 1.0 | 窗口透明度 (0.0-1.0) | Phase 2 |
| `model.current_model_path` | string | 默认模型 | 当前模型路径 | Phase 2 |
| `model.scale` | float | 1.0 | 模型缩放比例 | Phase 2 |
| `audio.volume` | float | 0.8 | 音量 (0.0-1.0) | Phase 3 |
| `audio.muted` | bool | false | 是否静音 | Phase 3 |
| `audio.audio_dir` | string | "audio/" | 音频文件独立存储目录 | Phase 3 |
| `behavior.drag_mode` | string | "direct" | 拖拽模式："direct" (直接跟随) / "physics" (物理惯性) | Phase 2 |
| `behavior.idle_interval_seconds` | int | 10 | 闲时动作触发间隔（秒） | Phase 2 |
| `system.auto_start` | bool | false | 开机自启 | Phase 2 |

---

## 三、Phase 2：模型行为映射 (model_config.json)

> **Phase 2 实现**：MVP 阶段 HitArea→动作映射硬编码在渲染器中。

每个模型目录下一份，定义该模型的点击响应和闲时行为：

```json
{
  "model_path": "models/haru/haru.model3.json",
  "hit_actions": {
    "Head": { "motion_group": "TapHead", "priority": 2 },
    "Body": { "motion_group": "TapBody", "priority": 2 }
  },
  "idle_motions": ["Idle", "IdleBreath"],
  "default_expression": "default"
}
```

| 字段 | 说明 |
|:---|:---|
| `hit_actions` | 点击区域 → 触发动作的映射。key 为 HitArea 名称，value 包含动作组名和优先级 |
| `idle_motions` | 闲时可用的动作组名列表，随机选择播放 |
| `default_expression` | 模型加载后的默认表情 |

---

## 四、Phase 3：音频映射配置 (audio_mapping.json)

> **Phase 3 实现**：音频文件与模型文件分离管理，映射关系由控制面板维护。

存储路径：`~/.config/desktop-pet/audio_mapping.json`

音频映射定义模型动作与音频文件的对应关系，支持跨模型复用：

```json
{
  "mappings": {
    "models/haru/haru.model3.json": {
      "TapHead": "audio/greeting.wav",
      "TapBody": "audio/laugh.wav",
      "Idle": null
    },
    "models/hiyori/hiyori.model3.json": {
      "TapHead": "audio/greeting.wav",
      "TapBody": "audio/surprise.wav"
    }
  }
}
```

| 字段 | 说明 |
|:---|:---|
| `mappings` | 顶层 key 为模型路径，value 为该模型的动作组→音频文件映射 |
| 动作组 key | 与 `model_config.json` 中的 `motion_group` 对应 |
| 音频文件 value | 指向独立音频目录（`audio/`）下的文件路径，`null` 表示无音频 |

**设计要点**：
- 同一音频文件（如 `audio/greeting.wav`）可被多个模型的不同动作引用，避免重复存储
- 模型加载完成后，控制面板查询该模型的音频映射，通过 `set_audio_mapping` 指令下发给渲染器
- 用户可在控制面板 UI 中自由编辑映射关系，无需修改模型文件
