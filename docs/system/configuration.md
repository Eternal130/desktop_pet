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

> **Phase 2 实现**：引入控制面板后，支持 JSON 配置文件读写和用户界面修改。音频播放由渲染器侧 `AudioManager` 实现（✅ miniaudio + libvorbis，OGG 播放），控制器侧音频映射管理（`audio_mapping.json`、`AudioMappingManager`/UI）待实现。

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
    "auto_start": false,
    "default_graphics_backend": "opengl"
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
| `audio.volume` | float | 0.8 | 音量 (0.0-1.0) | Phase 3b 渲染器侧 ✅ |
| `audio.muted` | bool | false | 是否静音 | Phase 3b 渲染器侧 ✅ |
| `audio.audio_dir` | string | "audio/" | 音频文件独立存储目录 | Phase 3b 控制器侧待实现 |
| `behavior.drag_mode` | string | "direct" | 拖拽模式："direct" (直接跟随) / "physics" (物理惯性) | Phase 2 |
| `behavior.idle_interval_seconds` | int | 10 | 闲时动作触发间隔（秒） | Phase 2 |
| `system.auto_start` | bool | false | 开机自启 | Phase 2 |
| `system.default_graphics_backend` | string | "opengl" | 渲染后端："opengl" (OpenGL) / "vulkan" (Vulkan) | Phase 2 |

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

## 四、Phase 2：面板配置 (panel.json) ✅ 已实现

> **Phase 2 实现**：引入多实例管理后，面板级配置独立于全局配置和实例配置。

存储路径：`~/.config/desktop-pet/panel.json`

```json
{
  "panel": {
    "x": 100,
    "y": 100,
    "width": 1200,
    "height": 760,
    "theme": "深紫梦幻"
  },
  "instances": [
    "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
    "f0e1d2c3-b4a5-6789-0fed-cba987654321"
  ]
}
```

| 字段 | 类型 | 默认值 | 说明 |
|:---|:---|:---|:---|
| `panel.x` | double | -1（居中） | 面板窗口 X 坐标 |
| `panel.y` | double | -1（居中） | 面板窗口 Y 坐标 |
| `panel.width` | double | 1200 | 面板窗口宽度 |
| `panel.height` | double | 760 | 面板窗口高度 |
| `panel.theme` | string | "深紫梦幻" | UI 主题名称 |
| `instances` | string[] | [] | 宠物实例配置 ID 列表（UUID），顺序即为显示顺序 |

**旧版迁移**：若存在 `panel-state.json`（旧格式），`PanelStateManager` 自动迁移——将旧实例数据拆分为独立 `instances/{uuid}.json`，备份旧文件为 `.bak`。

---

## 五、Phase 2：实例配置 (instances/{uuid}.json) ✅ 已实现

> **Phase 2 实现**：每个宠物实例拥有独立配置文件，由 `InstanceConfigManager` 管理。

存储路径：`~/.config/desktop-pet/instances/{uuid}.json`

```json
{
  "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "label": "锦瑟",
  "renderer_path": "/path/to/desktop-pet-renderer",
  "graphics_backend": "opengl",
  "model": {
    "name": "giwa-idol2023",
    "scale": 1.0
  },
  "window": {
    "x": 1200,
    "y": 600,
    "width": 400,
    "height": 500,
    "opacity": 1.0
  },
  "behavior": {
    "drag_mode": "direct",
    "idle_interval": 10,
    "target_fps": 0
  },
  "auto_start": false,
  "current_expression": "F01",
  "voice_pack": "锦瑟-锦瑟-中文-voice",
  "volume": 1.0
}
```

| 字段 | 类型 | 默认值 | 说明 |
|:---|:---|:---|:---|
| `id` | string | UUID | 实例唯一标识 |
| `label` | string | "新实例" | 实例显示名称 |
| `renderer_path` | string | "" | 渲染器可执行文件路径 |
| `graphics_backend` | string | "opengl" | 渲染后端（opengl / vulkan） |
| `model.name` | string | "" | 当前模型目录名 |
| `model.scale` | double | 1.0 | 模型缩放比例 |
| `window.x` / `window.y` | int | 1200 / 600 | 窗口位置 |
| `window.width` / `window.height` | int | 400 / 500 | 窗口尺寸 |
| `window.opacity` | double | 1.0 | 窗口透明度 |
| `behavior.drag_mode` | string | "direct" | 拖拽模式（direct / physics） |
| `behavior.idle_interval` | int | 10 | 闲时动作间隔（秒） |
| `behavior.target_fps` | int | 0 | 目标帧率（0 = 自适应，15-120 = 固定） |
| `auto_start` | bool | false | 随面板启动时自动启动 |
| `current_expression` | string | "F01" | 当前表情 |
| `voice_pack` | string\|null | null | 挂载的语音包目录名（null = 未挂载） |
| `volume` | double | 1.0 | 音量（0.0-1.0） |

---

## 六、Phase 3a：语音包挂载配置 (mount.json) ✅ 已实现

> **Phase 3a 实现**：语音包挂载关系由 `MountConfigManager` 管理。

存储路径：`~/.config/desktop-pet/mount.json`

```json
{
  "mounts": {
    "giwa-idol2023": {
      "voice_pack": "锦瑟-锦瑟-中文-voice"
    },
    "jinse-specialB": {
      "voice_pack": "锦瑟-锦瑟-中文-voice"
    }
  }
}
```

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `mounts` | object | 顶层 key 为模型目录名 |
| `voice_pack` | string\|null | 挂载的语音包目录名，`null` 表示未挂载 |

---

## 七、Phase 2：HitArea 缓存 (hit_area_cache.json) ✅ 已实现

> **Phase 2 实现**：缓存每个模型的 HitArea 列表，避免重复解析 model3.json。

存储路径：`~/.config/desktop-pet/hit_area_cache.json`

```json
{
  "giwa-idol2023": ["HitAreaHead", "HitAreaFace", "HitAreaChest", "HitAreaWaist"],
  "Hiyori": ["Head", "Body"]
}
```

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| 顶层 key | string | 模型目录名 |
| value | string[] | 该模型的 HitArea 名称列表 |

由 `HitAreaCacheManager` 在模型加载成功后自动更新。

---

## 八、Phase 3b：音频映射配置 (audio_mapping.json)（⚠️ 控制器侧待实现）

> **Phase 3b 实现**：音频文件与模型文件分离管理，映射关系由控制面板维护。引入语音包挂载后，挂载语音包的模型由 `meta.mko` 自动提供映射，无需此文件。
>
> **当前状态**：⚠️ 控制器侧待实现（`AudioMapping` record 仅定义，`AudioMappingManager`/UI 未实现）。渲染器侧 `play_audio` 命令已可用——由 `AudioManager`（miniaudio + libvorbis）提供 OGG 播放能力，错误码 7001/7002/7003 已定义。

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
- 挂载语音包的模型：由 `meta.mko` 自动提供完整映射，**不需要** `audio_mapping.json`
- 未挂载语音包的模型：仍可使用 `audio_mapping.json` 手动配置

---

## 九、配置文件总览

```
~/.config/desktop-pet/
├── config.json                  ← 全局配置（窗口位置、当前模型、行为参数）    [Phase 2 ✅]
├── panel.json                   ← 面板配置（窗口位置/主题/实例 ID 列表）     [Phase 2 ✅]
├── mount.json                   ← 语音包挂载配置（模型↔语音包关系）          [Phase 3a ✅]
├── hit_area_cache.json          ← HitArea 缓存（模型→HitArea 列表）         [Phase 2 ✅]
├── audio_mapping.json           ← 音频映射 [Phase 3b 控制器侧待实现；渲染器侧 play_audio 已接入 AudioManager]
└── instances/                   ← 实例配置目录                              [Phase 2 ✅]
    ├── {uuid-1}.json            ← 实例 1 的独立配置
    └── {uuid-2}.json            ← 实例 2 的独立配置
```
