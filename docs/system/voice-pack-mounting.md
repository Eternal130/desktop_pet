# 外置语音包挂载设计

> **Phase 3 实现**：本功能作为 Phase 3（音频模块）的核心能力，在现有音频架构（参见 [音频播放架构](../renderer/audio.md)）基础上，实现语音包与模型的解耦挂载。
> 系统设计概述参见 [系统设计](./README.md)，整体架构参见 [架构总览](../README.md)，配置文件参见 [配置文件设计](./configuration.md)。
>
> **当前实现状态**：Phase 3a（基础挂载）Java 侧已完成——`VoicePackScanner`、`MetaMkoParser`（Protobuf 解析）、`MountConfigManager`、`MountedBehaviorEngine` 及全部数据模型已实现并通过测试。渲染器侧 `play_motion_ext` 指令待实现。Phase 3b/3c/3d 待后续开发。

---

## 一、背景与动机

### 1.1 现状

当前 Live2D 模型与音频/动作的关系有两种形态：

| 形态 | 代表 | 内容 | 问题 |
|:---|:---|:---|:---|
| **常规模型** | `giwa-idol2023`、`jinse-specialB` | 模型骨骼（moc3）+ 纹理 + 物理 + HitAreas | 无动作、无音频、无口型 |
| **语音包** | `锦瑟-锦瑟-中文-voice` | 动作（motion3.json）+ 音频（ogg）+ 口型（txt）+ 行为模块（mkai） | 无模型骨骼、无纹理 |

两者互补但完全独立，当前系统无法将它们组合使用。

### 1.2 目标

实现 **"语音包 × 模型"自由组合**：

- 用户可将任意语音包挂载到任意常规模型上
- 挂载后，模型获得语音包的全部行为能力（语音、动作、口型、交互文案）
- 不修改语音包原始文件，不修改模型原始文件
- 同一语音包可挂载到多个模型，同一模型可切换不同语音包

---

## 二、资源结构分析

### 2.1 常规模型目录结构

以 `giwa-idol2023` 为例：

```
Resources/giwa-idol2023/
├── giwa-idol2023.model3.json        ← Live2D 模型定义（入口文件）
├── giwa-idol2023.moc3               ← 模型骨骼二进制（参数、网格、变形器）
├── giwa-idol2023.physics3.json      ← 物理模拟配置
└── giwa-idol2023.2048/              ← 纹理目录
    ├── texture_00.png
    └── texture_01.png
```

**model3.json 关键字段**：

```json
{
  "Version": 3,
  "FileReferences": {
    "Moc": "giwa-idol2023.moc3",
    "Textures": ["giwa-idol2023.2048/texture_00.png", "..."],
    "Physics": "giwa-idol2023.physics3.json"
    // 注意：无 Motions、无 Expressions
  },
  "Groups": [
    { "Target": "Parameter", "Name": "LipSync", "Ids": ["ParamMouthOpenY"] },
    { "Target": "Parameter", "Name": "EyeBlink", "Ids": [] }
  ],
  "HitAreas": [
    { "Id": "HitAreaHead",  "Name": "tap_head" },
    { "Id": "HitAreaFace",  "Name": "tap_face" },
    { "Id": "HitAreaChest", "Name": "tap_chest" },
    { "Id": "HitAreaWaist", "Name": "tap_waist" },
    { "Id": "HitAreaXXX",   "Name": "tap_x" },
    { "Id": "HitAreaLeg",   "Name": "tap_leg" },
    { "Id": "HitAreaArmL",  "Name": "tap_hand" },
    { "Id": "HitAreaArmR",  "Name": "tap_hand" }
  ]
}
```

**关键观察**：
- `FileReferences` 中无 `Motions` 节——模型自身不携带任何动作
- `HitAreas` 使用语义化命名（`tap_head`、`tap_face` 等）
- `LipSync` group 声明了口型参数 `ParamMouthOpenY`
- `EyeBlink` group 的 `Ids` 为空数组——该模型**不支持自动眨眼**。语音包 motion 中的眨眼效果通过直接操控 `ParamEyeLOpen`/`ParamEyeROpen` 参数实现，不受此限制

### 2.2 语音包目录结构

以 `锦瑟-锦瑟-中文-voice` 为例：

```
Resources/锦瑟-锦瑟-中文-voice/
├── meta.mko                          ← 二进制索引（事件→动作+音频+口型+文案映射）
├── audios/                           ← 语音文件（169 个 .ogg）
│   ├── 1.ogg ~ 144.ogg              ← 编号语音
│   └── app1.ogg ~ app25.ogg         ← 应用场景语音
├── lipSyncs/                         ← 口型时间轴数据
│   ├── 1.txt ~ 144.txt
│   └── app1.txt ~ app25.txt
├── motions/                          ← Live2D 动作文件（71 个 .motion3.json）
│   ├── idle01~04.motion3.json        ← 待机动作
│   ├── m01~m09.motion3.json          ← 通用动作
│   ├── 23~56.motion3.json            ← 交互动作（生日、摇晃、触摸等）
│   ├── 77~84.motion3.json            ← 夜间唤醒动作
│   ├── photo01~12.motion3.json       ← 拍照姿势
│   ├── sleep01~03.motion3.json       ← 睡眠动作
│   └── wakeup01.motion3.json         ← 唤醒动作
└── modules/                          ← 行为触发模块（11 个 .mkai，二进制）
    ├── alarm.mkai                    ← 闹钟
    ├── battery.mkai                  ← 电量
    ├── dressup.mkai                  ← 换装
    ├── idle.mkai                     ← 待机
    ├── lifecycle.mkai                ← 生命周期（早安/晚安/用餐等）
    ├── network.mkai                  ← 网络状态
    ├── shake.mkai                    ← 摇晃
    ├── tap.mkai                      ← 触摸交互
    ├── tsukkomi.mkai                 ← 吐槽
    ├── userevent.mkai                ← 用户事件（生日/邀约等）
    └── weather.mkai                  ← 天气
```

**关键观察**：
- 无 `.model3.json` / `.moc3` / 纹理——纯行为包，不含模型
- `meta.mko` 是全量索引，包含所有事件到资源的完整映射
- 动作文件使用标准 Live2D 参数（详见 [2.4 参数兼容性](#24-参数兼容性)）

### 2.3 meta.mko 索引格式

`meta.mko` 是语音包的核心索引文件，采用 **Protocol Buffers 3** 序列化格式（顶层 message 为 `Bundle`），包含完整的事件→资源映射。详细的 proto schema 定义、解析方案及示例代码见 [第八章](#八meta.mko-解析方案)。

**核心结构概览**：

```
Bundle
├── Meta         → 包元数据（名称、标识码、类型、文件清单）
├── modules[]    → 11 个 AI 行为模块（对应 modules/*.mkai）
├── groups[]     → 88 个动作分组定义（事件名 + 优先级 + 显示名）
├── actions[]    → 196 条动作映射（motion + audio + lipSync + 文案 + fadeIn/Out）
└── timings[]    → 定时事件（语音包中为空）
```

**Action 三种形态**（由 `Action` message 中字段是否为空决定）：

| 形态 | 说明 | 示例 group |
|:---|:---|:---|
| motion + audio + lipSync + doc | 完整交互（有动作、有语音、有精确口型、有文案） | `morning`、`tap_face`、`breakfast` |
| motion + audio + doc | 有动作、有语音、有文案，无独立口型文件 | `birthday`、`awake`、`shake` |
| motion only | 仅动作，无语音 | `idle`、`sleep_idle`、`photo_*` |

### 2.4 参数兼容性

语音包 motion 文件操控的 Live2D 参数与常规模型的参数对应关系：

| 参数 ID | 用途 | 语音包 motion 使用 | giwa-idol2023 支持 | jinse-specialB 支持 |
|:---|:---|:---:|:---:|:---:|
| `ParamAngleX/Y/Z` | 头部朝向 | ✓ | ✓ | ✓ |
| `ParamBodyAngleX/Y/Z` | 身体朝向 | ✓ | ✓ | ✓ |
| `ParamEyeLOpen` / `ParamEyeROpen` | 眼睛开合 | ✓ | ✓ | ✓ |
| `ParamEyeSmileL` / `ParamEyeSmileR` | 微笑眼 | ✓ | ✓ | ✓ |
| `ParamMouthOpenY` | 嘴巴开合（LipSync） | ✓ | ✓（LipSync group 已声明） | ✓ |
| `ParamBreath` | 呼吸 | ✓ | ✓ | ✓ |
| `ParamBodyAngleY2` / `ParamBodyAngleZ2` | 身体二级摆动 | ✓ | 取决于模型 | 取决于模型 |
| `ParamFish*`、`ParamEffect*` | 锦瑟专属特效 | ✓（但值为 0） | ✗（静默忽略） | 取决于模型 |

**兼容性结论**：
- Live2D SDK 对**不存在的参数会静默忽略**，不会崩溃
- 通用参数（头部、身体、眼睛、嘴巴、呼吸）在所有模型中广泛支持
- 锦瑟专属参数（Fish、Effect）在 motion 中值为 0，不产生可见影响
- **预期兼容度：95%+**，核心动作表现（头部运动、眼神、嘴型、身体摆动）可完整呈现

### 2.5 事件名与 HitArea Name 对齐

语音包中的交互事件名与模型 HitArea Name 已天然对齐：

| 语音包事件名 | 模型 HitArea Name | HitArea Id | 状态 |
|:---|:---|:---|:---:|
| `tap_head` | `tap_head` | `HitAreaHead` | ✅ 完全一致 |
| `tap_face` | `tap_face` | `HitAreaFace` | ✅ 完全一致 |
| `tap_chest` | `tap_chest` | `HitAreaChest` | ✅ 完全一致 |
| `tap_waist` | `tap_waist` | `HitAreaWaist` | ✅ 完全一致 |
| `tap_x` | `tap_x` | `HitAreaXXX` | ✅ 完全一致 |
| `tap_leg` | `tap_leg` | `HitAreaLeg` | ✅ 完全一致 |
| `tap_hand` | `tap_hand` | `HitAreaArmL/R` | ✅ 完全一致 |

这意味着 **HitArea 触发事件可直接映射到语音包事件，无需额外转换层**。

### 2.6 口型数据格式

`lipSyncs/*.txt` 使用制表符分隔的音素时间轴格式：

```
时间戳(秒)    音素字符
0.00          X          ← 静音
2.32          B          ← 音素 B
2.54          C
3.03          A
...
10.00         X          ← 静音
```

| 音素字符 | 含义 | ParamMouthOpenY 映射 |
|:---:|:---|:---|
| `X` | 静音 | 0.0 |
| `A` ~ `H` | 不同嘴型 | 映射到 0.0 ~ 1.0 范围（需标定） |

口型驱动目标参数为 `ParamMouthOpenY`，与模型 `LipSync` group 声明的参数一致。

---

## 三、系统架构

### 3.1 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                    控制面板 (Java)                                │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  语音包管理 UI                                              │  │
│  │  - 语音包列表 / 模型列表                                    │  │
│  │  - 挂载关系配置（模型 ↔ 语音包）                            │  │
│  │  - 参数兼容度预览                                           │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  语音包加载层                                               │  │
│  │  ┌──────────────────┐  ┌────────────────────────────────┐  │  │
│  │  │ VoicePackScanner │  │ MetaMkoParser                  │  │  │
│  │  │ 扫描识别语音包    │  │ 解析 meta.mko 二进制索引        │  │  │
│  │  └──────────────────┘  └────────────────────────────────┘  │  │
│  │  ┌──────────────────────────────────────────────────────┐  │  │
│  │  │ VoicePackInfo                                        │  │  │
│  │  │ 语音包元数据模型（事件列表、资源路径、模块定义）       │  │  │
│  │  └──────────────────────────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  挂载运行时                                                 │  │
│  │  ┌──────────────────┐  ┌────────────────────────────────┐  │  │
│  │  │ MountConfig      │  │ MountedBehaviorEngine          │  │  │
│  │  │ Manager          │  │ 事件分发 + 动作/音频/口型协调    │  │  │
│  │  │ 挂载关系持久化    │  │ 替代现有 InteractionHandler     │  │  │
│  │  └──────────────────┘  └────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  音频/口型层                                                │  │
│  │  ┌──────────────────┐  ┌────────────────────────────────┐  │  │
│  │  │ LipSyncDriver   │  │ TextBubble                     │  │  │
│  │  │ 口型数据→参数驱动 │  │ 交互文案气泡显示               │  │  │
│  │  └──────────────────┘  └────────────────────────────────┘  │  │
│  └────────────────────────────────────────────────────────────┘  │
└────────────────────────┬─────────────────────────────────────────┘
                         │  WebSocket (JSON Protocol)
                         ▼
┌──────────────────────────────────────────────────────────────────┐
│                    渲染引擎 (C++)                                │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  扩展指令                                                   │  │
│  │  - play_motion_ext（支持外部 motion 绝对路径）              │  │
│  │  - set_parameter（直接设置参数值，用于口型驱动）            │  │
│  └────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  音频播放器（Phase 3 新增）                                 │  │
│  │  - 接收 play_audio 指令 + 绝对路径                         │  │
│  │  - OpenAL 播放 .ogg 文件                                   │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 数据流

#### 场景 A：触摸交互（hit 事件触发）

```
① 用户点击模型头部
   ↓
② C++ Renderer 检测 HitArea → 发送 hit event { area_id: "HitAreaHead" }
   ↓
③ Java Controller 接收 hit event
   ↓
④ MountedBehaviorEngine 查找 HitArea 映射:
   HitAreaHead → Name "tap_head" → 查询挂载的语音包
   ↓
⑤ 从语音包 meta.mko 的 "tap_head" 事件组中随机选一条:
   → motion: motions/33.motion3.json
   → audio:  audios/33.ogg
   → text:   "抱歉，让阁下也替我担心了..."
   ↓
⑥ 并行下发:
   a) play_motion_ext { path: "<语音包绝对路径>/motions/33.motion3.json", priority: 2 }
   b) play_audio { path: "<语音包绝对路径>/audios/33.ogg" }
   c) UI 气泡显示文案
   d) 若有 lipSync → 启动 LipSyncDriver 定时驱动 ParamMouthOpenY
```

#### 场景 B：定时行为（Scheduler 触发）

```
① Scheduler 定时触发 idle 行为
   ↓
② MountedBehaviorEngine 查询语音包 "idle" 事件组
   → idle01~04.motion3.json（无音频）
   ↓
③ 随机选择一个 idle motion 下发:
   play_motion_ext { path: "<语音包绝对路径>/motions/idle02.motion3.json", priority: 1 }
```

#### 场景 C：生命周期行为（时间/系统事件触发）

```
① 系统检测到当前时间为早晨 7:00
   ↓
② MountedBehaviorEngine 查询语音包 "morning" 事件组
   → motion: m01.motion3.json + audio: 1.ogg + lipSync: 1.txt + text: "阁下昨夜睡得可否安稳？..."
   ↓
③ 下发完整交互（同场景 A 步骤 ⑥）
```

---

## 四、Java Controller 侧设计

### 4.1 新增类

| 类 | 包 | 职责 |
|:---|:---|:---|
| `VoicePackScanner` | `core` | 扫描 Resources 目录，识别含 `meta.mko` 的语音包目录 |
| `MetaMkoParser` | `core` | 解析 `meta.mko` 二进制文件，输出 `VoicePackInfo` |
| `VoicePackInfo` | `model` | 语音包元数据模型（角色名、分组列表、模块列表） |
| `VoicePackGroup` | `model` | 动作分组（事件名、优先级、action 列表） |
| `VoicePackAction` | `model` | 单条动作映射（id、motion、audio、lipSync、文案、fadeIn/Out） |
| `VoicePackModule` | `model` | 行为模块（key、优先级、文件路径） |
| `MountConfig` | `model` | 挂载配置（模型名 ↔ 语音包名 + 可选事件名映射覆盖） |
| `MountConfigManager` | `core` | 挂载关系的持久化读写 |
| `MountedBehaviorEngine` | `core` | 运行时行为引擎，替代 `InteractionHandler` 的事件分发 |
| `LipSyncDriver` | `core.audio` | 解析 lipSync txt，定时发送 `set_parameter` 驱动口型 |

### 4.2 修改类

| 类 | 修改内容 |
|:---|:---|
| `AppOrchestrator` | 启动时加载挂载配置，初始化 `MountedBehaviorEngine` |
| `InteractionHandler` | 当存在挂载语音包时，委托给 `MountedBehaviorEngine` 处理 |
| `Scheduler` | 扩展 idle motion 来源——优先使用语音包的 idle motions |
| `ModelScanner` | 新增 `scanAvailableVoicePacks()` 方法 |

### 4.3 VoicePackScanner

```java
public final class VoicePackScanner {

    /** 识别规则：目录中包含 meta.mko 文件 */
    public static List<String> scanAvailableVoicePacks(Path resourcesDir) {
        // 遍历 Resources/ 下所有子目录
        // 过滤包含 meta.mko 的目录
        // 返回语音包目录名列表
    }
}
```

与现有 `ModelScanner` 的区别：

| | ModelScanner | VoicePackScanner |
|:---|:---|:---|
| 识别标志 | `{name}.model3.json` | `meta.mko` |
| 扫描结果 | 模型名列表 | 语音包名列表 |
| 后续操作 | `ModelInfoParser.parse()` | `MetaMkoParser.parse()` |

### 4.4 VoicePackInfo 数据模型

```java
public record VoicePackInfo(
    String dirName,                             // 语音包目录名
    String displayName,                         // 显示名（Meta.name，如 "锦瑟-初级-中文"）
    String code,                                // 包标识码（Meta.code，如 "jinse-lv1-cn"）
    Path basePath,                              // 语音包绝对路径
    Map<String, VoicePackGroup> groups,         // 事件名 → 分组定义（含优先级 + action 列表）
    List<VoicePackModule> modules               // 行为模块列表
) {}

public record VoicePackGroup(
    String code,                // 分组代码 / 事件名（如 "morning"、"tap_head"）
    String name,                // 分组显示名（如 "早安"、"交互：触摸头部"）
    int priority,               // 分组优先级（1~5，用于动作中断判断）
    List<VoicePackAction> actions  // 该分组下的所有 action
) {}

public record VoicePackAction(
    int id,                     // 动作 ID（全局唯一）
    String motionPath,          // 相对路径（如 "motions/m01.motion3.json"），可为空
    String audioPath,           // 相对路径（如 "audios/1.ogg"），可为空
    String lipSyncPath,         // 相对路径（如 "lipSyncs/1.txt"），可为 null（optional 字段）
    String doc,                 // 交互文案，可为空
    long fadeInMs,              // 动作淡入时间（毫秒，如 1000）
    long fadeOutMs              // 动作淡出时间（毫秒，如 1000）
) {}

public record VoicePackModule(
    String key,                 // 模块标识（如 "idle"、"tap"）
    int priority,               // 模块优先级
    String filePath             // 模块文件路径（如 "modules/idle.mkai"）
) {}
```

### 4.5 MountConfig 配置

> **实现简化**：当前实现省略了 `eventOverrides` 字段，因为模型 HitArea Name 与语音包事件名已天然对齐（见 [2.5 事件名与 HitArea Name 对齐](#25-事件名与-hitarea-name-对齐)），暂不需要重映射。后续如遇特殊模型可扩展。

```java
public record MountConfig(
    String modelName,                        // 模型目录名
    String voicePackName                     // 语音包目录名，null 表示未挂载
) {}
```

### 4.6 MountedBehaviorEngine

核心行为引擎，替代现有的简单 HitAction 映射：

```java
public class MountedBehaviorEngine {

    private VoicePackInfo voicePack;         // 当前挂载的语音包
    private MountConfig mountConfig;         // 挂载配置
    private final Random random = new Random();

    /**
     * 处理 hit 事件。
     * 1. 将 HitArea Name 映射到语音包事件名
     * 2. 从事件组中随机选择一条
     * 3. 构建并下发 motion + audio + lipSync + text
     */
    public void handleHitEvent(String hitAreaName) { ... }

    /**
     * 获取 idle 动作列表，用于 Scheduler。
     */
    public List<String> getIdleMotionPaths() { ... }

    /**
     * 处理生命周期/系统事件（morning、night、battery 等）。
     */
    public void handleSystemEvent(String eventName) { ... }
}
```

### 4.7 LipSyncDriver

基于 lipSync txt 文件中的时间轴数据，定时发送参数更新指令：

```java
public class LipSyncDriver {

    /**
     * 解析 lipSync 文件内容。
     * 格式：每行 "时间戳\t音素字符"
     * 返回按时间排序的 (时间, 参数值) 列表。
     */
    public List<LipSyncFrame> parse(Path lipSyncFile) { ... }

    /**
     * 启动口型驱动。
     * 在独立线程中按时间轴发送 set_parameter 指令。
     * 当音频播放结束或被中断时停止。
     */
    public void start(List<LipSyncFrame> frames, Consumer<Float> parameterSender) { ... }

    public void stop() { ... }
}

public record LipSyncFrame(double timestamp, float mouthOpenY) {}
```

**音素到参数值的映射**（初始标定，可后续调优）：

| 音素 | 含义 | ParamMouthOpenY |
|:---:|:---|:---:|
| `X` | 静音 | 0.0 |
| `A` | 开口元音 | 1.0 |
| `B` | 闭唇辅音 | 0.2 |
| `C` | 半开 | 0.5 |
| `D` | 舌尖音 | 0.4 |
| `E` | 半闭元音 | 0.7 |
| `F` | 唇齿音 | 0.3 |
| `G` | 后元音 | 0.8 |
| `H` | 气息音 | 0.1 |

---

## 五、C++ Renderer 侧改动

### 5.1 新增/修改指令

#### `play_motion_ext` — 从外部路径播放动作

```json
{
  "type": "command", "action": "play_motion_ext",
  "id": "...",
  "payload": {
    "motion_path": "/absolute/path/to/motions/m01.motion3.json",
    "priority": 2,
    "fade_in": 0.5,
    "fade_out": 0.5
  },
  "timestamp": ...
}
```

| 字段 | 类型 | 必填 | 说明 |
|:---|:---|:---:|:---|
| `motion_path` | string | ✓ | motion3.json 文件的绝对路径 |
| `priority` | int | 否 | 动作优先级（默认 2） |
| `fade_in` | float | 否 | 淡入时间秒数（默认 1.0） |
| `fade_out` | float | 否 | 淡出时间秒数（默认 1.0） |

> **单位转换**：proto `Action.fadeIn/fadeOut` 存储为 `int64` 毫秒（如 `1000`），发送 Renderer 指令时需转为秒（`1000 → 1.0`）。当前语音包中所有 action 统一为 fadeIn=1000, fadeOut=1000。

**与现有 `play_motion` 的区别**：
- `play_motion`：通过 `group` + `index` 从 model3.json 中查找 motion 文件
- `play_motion_ext`：直接传入绝对路径，绕过 model3.json 的 Motions 定义

**Renderer 实现要点**：
- 直接使用 Cubism Framework 的 `CubismMotion::Create()` 加载任意路径的 motion3.json
- 加载后的 motion 应用到当前模型上（参数匹配由 SDK 自动处理，不存在的参数静默忽略）
- 缓存已加载的 motion 数据（LRU），避免重复 I/O

#### `set_parameter` — 直接设置模型参数

```json
{
  "type": "command", "action": "set_parameter",
  "id": "...",
  "payload": {
    "param_id": "ParamMouthOpenY",
    "value": 0.7,
    "weight": 1.0,
    "duration_ms": 50
  },
  "timestamp": ...
}
```

| 字段 | 类型 | 必填 | 说明 |
|:---|:---|:---:|:---|
| `param_id` | string | ✓ | 参数 ID（如 `ParamMouthOpenY`） |
| `value` | float | ✓ | 参数值 |
| `weight` | float | 否 | 权重（默认 1.0） |
| `duration_ms` | int | 否 | 持续时间，超时后参数恢复（默认 0 = 永久生效直到下次设置） |

**用途**：
- Java 端 LipSyncDriver 定时发送 `set_parameter` 驱动口型
- 比在 Renderer 端解析 lipSync txt 更灵活（Controller 可根据音频播放状态动态调整）

#### `play_audio` — 播放音频文件（Phase 3 已预留）

参见 [Commands — Phase 3 指令](../protocol/commands.md#10-phase-3-指令待实现)。扩展 payload 支持绝对路径：

```json
{
  "type": "command", "action": "play_audio",
  "id": "...",
  "payload": {
    "audio_path": "/absolute/path/to/audios/33.ogg"
  },
  "timestamp": ...
}
```

### 5.2 改动范围评估

| 文件 | 改动 | 工作量 |
|:---|:---|:---:|
| `CommandHandlers.cpp` | 注册 `play_motion_ext`、`set_parameter` 处理器 | 中 |
| `LAppModel.cpp` | 新增 `StartMotionFromFile(path, priority, fadeIn, fadeOut)` 方法 | 中 |
| `LAppModel.cpp` | 新增 `SetParameterValue(paramId, value, weight)` 方法 | 小 |
| 音频播放器 | Phase 3 新模块，支持 OGG 播放（OpenAL + stb_vorbis / dr_libs） | 大 |

---

## 六、配置文件设计

### 6.1 挂载配置文件

存储路径：`~/.config/desktop-pet/mount.json`

```json
{
  "mounts": {
    "giwa-idol2023": {
      "voice_pack": "锦瑟-锦瑟-中文-voice",
      "event_overrides": {}
    },
    "jinse-specialB": {
      "voice_pack": "锦瑟-锦瑟-中文-voice",
      "event_overrides": {
        "tap_extra": "tap_hand"
      }
    }
  }
}
```

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `mounts` | object | 顶层 key 为模型目录名 |
| `voice_pack` | string | 挂载的语音包目录名，`null` 表示未挂载 |
| `event_overrides` | object | 可选。key 为模型 HitArea Name，value 为语音包事件名。用于模型独有的 HitArea 映射到语音包中最接近的事件 |

### 6.2 与现有配置的关系

```
~/.config/desktop-pet/
├── config.json               ← 全局配置（窗口位置、当前模型、行为参数）[已实现]
├── mount.json                ← 挂载配置（模型↔语音包关系）[新增]
└── audio_mapping.json        ← 音频映射（Phase 3 原有设计）[调整]
```

**`audio_mapping.json` 的定位调整**：

原设计中 `audio_mapping.json` 用于手动配置 "motionGroup → audioFile" 映射。引入语音包挂载后：

- 语音包挂载的模型：由 `meta.mko` 自动提供完整的动作-音频映射，**不需要** `audio_mapping.json`
- 未挂载语音包的模型：仍可使用 `audio_mapping.json` 手动配置
- 两者为**互斥关系**：挂载语音包时忽略 `audio_mapping.json`，卸载语音包后回退到 `audio_mapping.json`

---

## 七、事件类型全表

> 以下数据由 `protoc --decode=Bundle bundles.proto < meta.mko` 解码验证，共 **88 个 ActionGroup、196 条 Action**。

### 7.1 生命周期事件（9 组，27 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `morning` | 早安 | 3 | 早晨时段 | 3 |
| `night` | 晚安 | 3 | 晚间时段 | 3 |
| `breakfast` | 早饭 | 3 | 早餐时段 | 2 |
| `lunch` | 午饭 | 3 | 午餐时段 | 2 |
| `dinner` | 晚饭 | 3 | 晚餐时段 | 2 |
| `sleeping` | 即将入睡 | 1 | 深夜 | 1 |
| `sleepy` | 休眠 | 3 | 长时间无操作 | 3 |
| `awake` | 夜间唤醒 | 4 | 深夜唤醒 | 8 |
| `morning_resume` | 问早（滑屏） | 5 | 早晨解锁 | 3 |

### 7.2 交互事件（8 组，32 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `tap_head` | 触摸头部 | 2 | HitArea 点击 | 4 |
| `tap_face` | 触摸面部 | 2 | HitArea 点击 | 4 |
| `tap_chest` | 触摸胸部 | 2 | HitArea 点击 | 4 |
| `tap_hand` | 触摸手部 | 2 | HitArea 点击 | 4 |
| `tap_waist` | 触摸腰部 | 2 | HitArea 点击 | 4 |
| `tap_x` | 触摸敏感区 | 2 | HitArea 点击 | 4 |
| `tap_leg` | 触摸腿部 | 2 | HitArea 点击 | 4 |
| `shake` | 摇晃 | 2 | 摇晃检测 | 4 |

### 7.3 系统事件（14 组，28 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `lowpower` | 低电量 | 3 | 电量低 | 2 |
| `charging` | 充电中 | 3 | 插入充电 | 2 |
| `charged` | 充电完成 | 4 | 充满电 | 2 |
| `network_yes` | 有网络 | 4 | 网络恢复 | 2 |
| `network_no` | 无网络 | 4 | 网络断开 | 2 |
| `wifi_yes` | 有 WiFi | 3 | WiFi 连接 | 2 |
| `wifi_no` | 无 WiFi | 3 | WiFi 断开 | 2 |
| `weather_sun` | 天气：晴 | 3 | 天气变化 | 2 |
| `weather_cloudy` | 天气：阴 | 3 | 天气变化 | 2 |
| `weather_rain` | 天气：雨 | 3 | 天气变化 | 2 |
| `weather_snow` | 天气：雪 | 3 | 天气变化 | 2 |
| `weather_fog` | 天气：雾 | 3 | 天气变化 | 2 |
| `weather_haze` | 天气：霾 | 3 | 天气变化 | 2 |
| `error` | 报错 | 3 | 系统错误 | 2 |

### 7.4 用户事件（10 组，21 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `birthday` | 生日 | 3 | 用户生日 | 2 |
| `birthday_friend` | 朋友生日 | 3 | 朋友生日 | 2 |
| `homework` | 作业 | 3 | 学习时段 | 2 |
| `work` | 工作 | 3 | 工作时段 | 2 |
| `school_go` | 上学 | 3 | 上学时间 | 2 |
| `work_go` | 上班 | 3 | 上班时间 | 2 |
| `invite` | 朋友邀约 | 3 | 社交事件 | 2 |
| `first_time` | 初次打开 | 2 | 首次启动 | 3 |
| `random_homework` | 作业（不定时） | 3 | 随机触发 | 2 |
| `random_work` | 工作（不定时） | 3 | 随机触发 | 2 |

### 7.5 应用场景事件（9 组，25 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `tv.danmaku.bili` | Bilibili | 2 | 特定 app | 4 |
| `app_chat` | 聊天类 | 2 | 聊天 app | 4 |
| `app_shopping` | 购物类 | 2 | 购物 app | 3 |
| `app_sns` | 社交网络 | 2 | 社交 app | 3 |
| `app_music` | 音乐类 | 2 | 音乐 app | 3 |
| `app_dining` | 餐饮类 | 2 | 餐饮 app | 2 |
| `app_groupon` | 团购类 | 2 | 团购 app | 2 |
| `app_shortvideo` | 短视频类 | 2 | 短视频 app | 2 |
| `app_game` | 游戏类 | 2 | 游戏 app | 2 |

### 7.6 特殊动作事件（仅 motion，无音频）（15 组，20 条 action）

| 事件名 | 中文 | priority | 用途 | action 数 |
|:---|:---|:---:|:---|:---:|
| `idle` | 待机 | 1 | Scheduler 定时触发 | 4 |
| `sleep_idle` | 睡觉待机 | 1 | 深夜待机动画 | 3 |
| `dressup` | 换装 | 3 | 换装动画 | 2 |
| `photo_smile` | 拍照：微笑 | 2 | 拍照功能 | 1 |
| `photo_cute` | 拍照：可爱 | 2 | 拍照功能 | 1 |
| `photo_mischef` | 拍照：调皮 | 2 | 拍照功能 | 1 |
| `photo_delight` | 拍照：高兴 | 2 | 拍照功能 | 1 |
| `photo_shy` | 拍照：害羞 | 2 | 拍照功能 | 1 |
| `photo_expect` | 拍照：期待 | 2 | 拍照功能 | 1 |
| `photo_reflect` | 拍照：思考 | 2 | 拍照功能 | 1 |
| `photo_pity` | 拍照：可怜 | 2 | 拍照功能 | 1 |
| `photo_sleepy` | 拍照：犯困 | 2 | 拍照功能 | 1 |
| `photo_worry` | 拍照：担忧 | 2 | 拍照功能 | 1 |
| `photo_amaze` | 拍照：惊讶 | 2 | 拍照功能 | 1 |
| `photo_doubt` | 拍照：疑惑 | 2 | 拍照功能 | 1 |

### 7.7 通用反馈事件（15 组，30 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `yes` | 肯定句 | 3 | 用户确认 | 2 |
| `sorry` | 道歉 | 3 | 异常恢复 | 2 |
| `responses` | 回应 | 3 | 通用呼叫 | 2 |
| `feed` | 喂食 | 2 | 喂食操作 | 2 |
| `random_ask` | 不定时询问 | 3 | 随机触发 | 2 |
| `random_cute` | 不定时卖萌 | 3 | 随机触发 | 2 |
| `outdoor` | 出去走走 | 3 | 外出建议 | 2 |
| `eat` | 吃点东西 | 3 | 进食建议 | 2 |
| `buy` | 买点东西 | 3 | 购物建议 | 2 |
| `go_home` | 回家 | 5 | 回家问候 | 2 |
| `cleaned` | 清理完成 | 3 | 清理操作 | 2 |
| `ban_update` | 新番更新 | 3 | 内容更新 | 2 |
| `schedule` | 日程提醒 | 3 | 日程事件 | 2 |
| `resume_short` | 短时离开归来 | 4 | 锁屏 1-3 小时后 | 2 |
| `resume_long` | 长时离开归来 | 4 | 锁屏 5-8 小时后 | 2 |

### 7.8 社交/养成事件（8 组，13 条 action）

| 事件名 | 中文 | priority | 触发条件 | action 数 |
|:---|:---|:---:|:---|:---:|
| `receive_gift` | 收到礼物 | 2 | 礼物交互 | 2 |
| `give_gift` | 赠送礼物 | 2 | 赠送操作 | 2 |
| `message` | 短信 | 2 | 短信事件 | 2 |
| `accept_mission` | 接取任务 | 2 | 角色任务 | 1 |
| `finish_mission` | 完成任务 | 2 | 任务完成 | 1 |
| `travel` | 出游 | 2 | 出游操作 | 1 |
| `return` | 出游归来 | 2 | 归来事件 | 2 |
| `level_up` | 升级 | 2 | 角色升级 | 2 |

---

## 八、meta.mko 解析方案

> **状态：已确认** — 通过分析 mimikko 客户端（模型来源厂商）的反编译代码，确认 `meta.mko` 为标准 **Protocol Buffers 3** 序列化文件，顶层 message 类型为 `Bundle`。

### 8.1 Protobuf Schema（bundles.proto）

以下为从 mimikko 客户端提取的完整 proto 定义，`meta.mko` 文件即为 `Bundle` message 的二进制序列化：

```protobuf
syntax = "proto3";

option java_package = "com.mimikko.app.lib.bundle";
option java_multiple_files = true;

enum BundleType {
  MODEL = 0;     // 模型包
  AI = 1;        // AI 语音包（meta.mko 对应此类型）
  SHOW = 2;      // 展示包
  STYLE = 3;     // 风格包
  EXP = 4;       // 表情包
  ENV = 5;       // 环境包
}

message Meta {
  string name = 1;              // 显示名（如 "锦瑟-初级-中文"）
  string code = 2;              // 包标识码
  BundleType type = 3;          // 包类型（语音包为 AI = 1）
  string root = 4;              // 根目录路径
  repeated FileEntity files = 5; // 包含的文件列表
}

message Bundle {
  Meta meta = 1;                        // 包元数据
  repeated Theme themes = 2;            // 主题列表
  repeated AiModule modules = 3;        // AI 行为模块（对应 modules/*.mkai）
  repeated ActionGroup groups = 4;      // 动作分组定义
  repeated Action actions = 5;          // ★ 核心：动作-音频-口型-文案映射
  repeated Timing timings = 6;          // 定时事件（环境/表情切换）
}

message ActionGroup {
  string code = 1;              // 分组代码（即事件名，如 "morning"、"tap_head"）
  int32 priority = 2;           // 优先级
  string name = 3;              // 分组显示名（如 "早安"、"交互：触摸头部"）
}

message Action {
  int32 id = 1;                 // 动作 ID（全局唯一）
  string group = 2;             // 所属分组代码（关联 ActionGroup.code）
  string motion = 3;            // 动作文件路径（如 "motions/m01.motion3.json"）
  string audio = 4;             // 音频文件路径（如 "audios/1.ogg"）
  optional string lipSync = 5;  // 口型文件路径（如 "lipSyncs/1.txt"），可为空
  string doc = 6;               // 交互文案（如 "阁下昨夜睡得可否安稳？..."）
  int64 fadeIn = 7;             // 动作淡入时间（毫秒）
  int64 fadeOut = 8;            // 动作淡出时间（毫秒）
}

message AiModule {
  string key = 1;               // 模块标识（如 "idle"、"tap"、"weather"）
  int32 priority = 2;           // 模块优先级
  string file = 3;              // 模块文件路径（如 "modules/idle.mkai"）
}

message Theme {
  string name = 1;              // 主题名
  string code = 2;              // 主题代码
  string root = 3;              // 主题根目录
  string file = 4;              // 主题配置文件路径
  string color = 5;             // 主题颜色
  bool isDefault = 6;           // 是否默认主题
}

message Timing {
  float time = 1;               // 触发时间
  oneof content {
    Env env = 2;                // 环境切换
    Exp exp = 3;                // 表情切换
    Wake wake = 4;              // 唤醒事件
  }
}

message Env {
  string image = 1;             // 背景图片
  optional string video = 2;    // 背景视频（可选）
}

message Exp {
  string background = 1;        // 背景动作
  string action = 2;            // 表情动作
  string idle = 3;              // 待机动作
}

message Wake {
  bool isAwake = 1;             // 是否唤醒
}

message FileEntity {
  string path = 1;              // 文件相对路径
  bool encrypt = 2;             // 是否加密
}
```

### 8.2 数据关系图

```
Bundle
├── Meta
│   ├── name: "锦瑟-初级-中文"               ← 显示名
│   ├── code: "jinse-lv1-cn"               ← 包标识码（独立字段）
│   ├── type: AI (1)
│   └── files[]: 所有文件清单 (path + encrypt 标记)
├── modules[]: AiModule（11 个模块）
│   ├── {key:"idle",    priority:1, file:"modules/idle.mkai"}
│   ├── {key:"tap",     priority:1, file:"modules/tap.mkai"}
│   ├── {key:"weather", priority:1, file:"modules/weather.mkai"}
│   └── ...
├── groups[]: ActionGroup（88 个分组，priority 1~5）
│   ├── {code:"idle",      priority:1, name:"待机"}
│   ├── {code:"tap_head",  priority:2, name:"交互：触摸头部"}
│   ├── {code:"morning",   priority:3, name:"早安"}
│   ├── {code:"resume_short", priority:4, name:"锁屏后1-3小时开启launcher"}
│   ├── {code:"morning_resume", priority:5, name:"问早（滑屏）"}
│   └── ...
├── actions[]: Action（196 条动作）             ← ★ 核心数据
│   ├── {id:1,  group:"morning",  motion:"motions/m01.motion3.json",
│   │    audio:"audios/1.ogg", lipSync:"lipSyncs/1.txt",
│   │    doc:"阁下昨夜睡得可否安稳？...", fadeIn:1000, fadeOut:1000}
│   ├── {id:2,  group:"morning",  motion:"motions/m01.motion3.json",
│   │    audio:"audios/2.ogg", lipSync:"lipSyncs/2.txt",
│   │    doc:"阁下早安，眼下时辰尚早...", fadeIn:1000, fadeOut:1000}
│   ├── {id:28, group:"tap_head", motion:"motions/33.motion3.json",
│   │    audio:"audios/33.ogg", lipSync:null,
│   │    doc:"抱歉，让阁下也替我担心了...", fadeIn:1000, fadeOut:1000}
│   └── ...
└── timings[]: Timing（定时事件，语音包中为空）
```

### 8.3 解析实现方案

**Java 端解析只需一行**：

```java
// 读取 meta.mko 并反序列化为 Bundle 对象
Bundle bundle = Bundle.parseFrom(Files.readAllBytes(Path.of("Resources/锦瑟-锦瑟-中文-voice/meta.mko")));

// 访问元数据
String name = bundle.getMeta().getName();       // "锦瑟-初级-中文jinse-lv1-cn"
BundleType type = bundle.getMeta().getType();   // AI

// 遍历所有动作分组
for (ActionGroup group : bundle.getGroupsList()) {
    System.out.println(group.getCode() + " → " + group.getName());
}

// 按分组查找动作
Map<String, List<Action>> actionsByGroup = bundle.getActionsList().stream()
    .collect(Collectors.groupingBy(Action::getGroup));

// 获取 tap_head 的所有动作
List<Action> tapHeadActions = actionsByGroup.get("tap_head");
for (Action action : tapHeadActions) {
    System.out.println("motion=" + action.getMotion());
    System.out.println("audio=" + action.getAudio());
    System.out.println("lipSync=" + action.getLipSync());  // 可能为空
    System.out.println("doc=" + action.getDoc());
}
```

### 8.4 依赖引入

在 Java 项目中引入 protobuf 运行时（controller/pom.xml）：

```xml
<dependency>
    <groupId>com.google.protobuf</groupId>
    <artifactId>protobuf-java</artifactId>
    <version>4.29.3</version>
</dependency>
```

> **版本说明**：protobuf v4.x 对应 protoc v28+（新版本号体系）。需确保 `protobuf-java` 运行时版本与编译用的 `protoc` 版本兼容。当前开发环境 protoc 为 `libprotoc 32.1`，对应 protobuf-java 4.29.x。

使用 `protoc` 编译 `bundles.proto` 生成 Java 类：

```bash
protoc --java_out=src/main/java bundles.proto
```

> **注意**：proto 文件中的 `option java_package = "com.mimikko.app.lib.bundle"` 是 mimikko 原始包名。编译前应修改为本项目的包名（如 `com.desktoppet.bundle`），以保持代码库一致性。

生成的类包含完整的 `Bundle.parseFrom()` / `Meta` / `Action` / `ActionGroup` / `AiModule` 等反序列化支持。

### 8.5 .mkai 模块文件

`.mkai` 文件是 `graph_data.proto` 中定义的 `GraphData` message 的序列化：

```protobuf
message GraphData {
  repeated Node nodes = 1;      // 行为图节点
  repeated Edge edges = 2;      // 节点间连接
}
```

行为图中的事件节点数据使用 `graph_event.proto` 中的 `StageEvent` message，通过 `google.protobuf.Any` 嵌入到 `Node.data` 字段中。事件类型涵盖：

| 事件 message | 触发条件 |
|:---|:---|
| `Idle` | 待机触发（无参数） |
| `Tap { area }` | 触摸交互（area = HitArea 名） |
| `Shake` | 摇晃检测 |
| `Lifecycle { alive }` | 生命周期事件 |
| `Battery { effect }` | 电量事件（LOW/CHARGED/CHARGING） |
| `Network { effect }` | 网络事件（NETWORK_LOST/WIFI_LOST/MONET/WIFI） |
| `Weather { weather }` | 天气事件（SUNNY/CLOUDY/RAINY/SNOWY/FOGGY/HAZY） |
| `Alarm { type }` | 定时事件（MORNING/NIGHT/BREAKFAST 等 14 种） |
| `Tsukkomi { type }` | 吐槽事件 |
| `DressUp { character }` | 换装事件 |
| `UserEvent { type }` | 用户事件（HOME/BIRTHDAY/THINKING/ANSWER） |

> **注意**：`.mkai` 的解析对于基础挂载功能**非必需**——`Bundle.actions` 和 `Bundle.groups` 已包含完整的事件→动作映射。`.mkai` 的行为图仅用于复杂的条件触发逻辑（如天气+时间段组合），可在后续阶段实现。

---

## 九、实现阶段规划

### Phase 3a — 基础挂载（最小可用）

| 步骤 | 内容 | 依赖 | 状态 |
|:---|:---|:---|:---:|
| 1 | `VoicePackScanner` — 扫描识别语音包 | 无 | ✅ 已完成 |
| 2 | `MountConfig` + `MountConfigManager` — 挂载配置持久化 | 无 | ✅ 已完成 |
| 3 | `play_motion_ext` 指令 — Renderer 支持外部路径 motion | Renderer C++ | ❌ 待实现 |
| 4 | 引入 protobuf-java 依赖 + `protoc` 编译 `bundles.proto` 生成 Java 类 | 无 | ✅ 已完成 |
| 5 | `MetaMkoParser` + `MountedBehaviorEngine` — hit 事件 → 语音包 motion | 步骤 2, 3, 4 | ✅ Java 侧已完成（依赖步骤 3 渲染器指令后联调） |
| 6 | UI 扩展 — Settings Tab 语音包选择 ComboBox | 步骤 1, 2 | ✅ 已完成 |

**Phase 3a 当前状态**：Java 侧全部完成。用户可在 Settings Tab 选择语音包挂载，`MountedBehaviorEngine` 可生成 `play_motion_ext` 指令。待渲染器实现 `play_motion_ext` 指令后即可联调。

### Phase 3b — 音频播放

| 步骤 | 内容 | 依赖 |
|:---|:---|:---|
| 7 | Renderer 音频播放器 — OpenAL + OGG 支持 | Renderer C++ |
| 8 | `play_audio` 指令实现 | 步骤 7 |
| 9 | `MountedBehaviorEngine` 扩展 — 同步下发 motion + audio | 步骤 5, 8 |

**Phase 3b 交付物**：交互时同时播放动作和语音。

### Phase 3c — 口型同步 + 文案

| 步骤 | 内容 | 依赖 |
|:---|:---|:---|
| 10 | `set_parameter` 指令 — Renderer 支持参数直接设置 | Renderer C++ |
| 11 | `LipSyncDriver` — lipSync txt 解析 + 定时驱动 | 步骤 10 |
| 12 | 文案气泡 UI | JavaFX UI |
| 13 | `MountedBehaviorEngine` 完整集成 — motion + audio + lipSync + text | 步骤 9, 11, 12 |

**Phase 3c 交付物**：完整的交互体验——动作 + 语音 + 口型同步 + 文案气泡。

### Phase 3d — 行为图引擎（.mkai）

| 步骤 | 内容 | 依赖 |
|:---|:---|:---|
| 14 | 解析 `graph_data.proto` + `graph_event.proto` 定义的行为图 | protobuf 依赖 |
| 15 | 实现 `GraphRuntime` 行为图执行引擎 | 步骤 14 |
| 16 | 集成条件触发逻辑（天气+时间段组合等） | 步骤 15 |

> **注意**：meta.mko 的 Protobuf schema 已完全确认（见第八章），Phase 3a 即可直接使用 `Bundle.parseFrom()` 解析，无需中间 JSON 格式。

---

## 十、风险与待决事项

| 风险 | 影响 | 缓解措施 | 状态 |
|:---|:---|:---|:---:|
| ~~meta.mko 格式未知~~ | ~~无法自动解析语音包~~ | ✅ 已解决 — Protobuf3 `Bundle` message，schema 完整确认 | ✅ 已解决 |
| ~~Java 侧解析/挂载~~ | ~~无法使用语音包~~ | ✅ 已解决 — `MetaMkoParser` + `MountedBehaviorEngine` 已实现并通过测试 | ✅ 已解决 |
| .mkai 行为图复杂度 | 条件触发逻辑需要图执行引擎 | Phase 3d 实现；基础挂载仅需 `Bundle.actions` 即可工作 | 待实现 |
| motion 参数兼容度 | 部分模型可能缺少参数导致动作不完整 | SDK 静默忽略缺失参数；可增加兼容度检测 UI | 待验证 |
| 口型参数映射精度 | 音素→ParamMouthOpenY 映射可能不准确 | 初始映射表 + 可调参数，通过实际效果迭代优化 | 待实现 |
| play_motion_ext 性能 | 频繁从磁盘加载 motion 文件 | Renderer 端增加 motion 缓存（LRU） | 待实现 |
| lipSync 驱动精度 | WebSocket 延迟影响口型同步 | 采用本地时钟驱动，提前 buffer；或改为 Renderer 端驱动 | 待实现 |
