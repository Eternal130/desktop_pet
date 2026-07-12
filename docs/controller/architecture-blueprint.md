# 控制面板架构蓝图（跨实现参考）

> **文档定位**：本文档是**架构无关**的控制面板功能规格与接口契约，用于在**任意技术栈**中重新实现等效的控制面板（如 Qt、Electron、.NET WPF/WinUI、Flutter Desktop、Tauri、Web+本地服务、甚至另一个 Java 框架）。
>
> 现有 JavaFX 参考实现的具体类名、第三方库选型见 [控制面板设计 (Java)](./README.md)；本文档剥离这些实现细节，只保留**必须复刻的功能行为**与**必须遵守的接口契约**。
>
> **读者**：准备在新技术栈中实现桌面宠物控制面板的工程师。阅读本文档后，应能不依赖 Java 参考实现源码，独立产出一个与现有 C++ 渲染引擎完全互通的控制面板。

---

## 一、控制面板的核心职责

一句话概括：**控制面板是桌面宠物系统的"大脑与控制台"——它管理用户配置、编排渲染器子进程、通过 WebSocket 下达指令并接收事件、提供图形化交互界面，而把所有高性能图形渲染工作完全委托给独立的 C++ 渲染器进程。**

由此衍生出七项不可削减的职责：

| # | 职责 | 说明 |
|:---:|:---|:---|
| 1 | **进程编排** | 启动/停止/重启/监控一个或多个渲染器子进程，处理崩溃恢复 |
| 2 | **通信中枢** | 充当 WebSocket **服务端**，接受渲染器（客户端）连接，收发 JSON 消息 |
| 3 | **配置管理** | 持久化用户偏好（窗口位置、主题、实例列表、挂载关系等）到磁盘 |
| 4 | **用户界面** | 提供图形化控制台：实例管理、模型/语音包切换、参数调节、日志查看 |
| 5 | **运行时决策** | 闲时动作调度、点击交互响应、状态同步 |
| 6 | **系统集成** | 系统托盘、开机自启、多平台支持（至少 Windows + Linux） |
| 7 | **资源监控** | 采集并展示自身与渲染器的 CPU/内存/GPU/显存占用 |

> **职责边界提醒**：控制面板**不**负责模型加载、动画渲染、点击检测（底层）、音频播放——这些由渲染器承担。控制面板通过指令驱动渲染器执行这些操作。

---

## 二、整体架构

### 2.1 进程拓扑

```
┌────────────────────────────────────────────────────────────────────┐
│                    控制面板进程（本文档范围）                        │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                        用户界面层                              │  │
│  │   主窗口  │  设置页  │  资源监视页  │  系统托盘  │  对话框       │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                       业务逻辑层                              │  │
│  │  实例管理 │ 配置持久化 │ 闲时调度 │ 交互响应 │ 资源采集 │ 扫描解析│  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              通信层（WebSocket Server）                        │  │
│  └──────────────────────────────────────────────────────────────┘  │
└──────────────────────────┬─────────────────────────────────────────┘
                           ▲
                           │ WebSocket（JSON，端口 9001，本地回环）
                           │
┌──────────────────────────┴─────────────────────────────────────────┐
│                  渲染器进程（C++，由控制面板启动）                    │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              WebSocket Client + 主循环消息泵                   │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │       Live2D 渲染 │ 音频播放 │ 点击/拖拽检测 │ 字幕             │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────┘
```

### 2.2 关键架构原则

| 原则 | 说明 | 不可妥协的理由 |
|:---|:---|:---|
| **方向反转** | 控制面板是 WebSocket **Server**；渲染器是 **Client** | 控制面板是常驻进程，渲染器由它启动；让常驻方监听、短命方连接，避免端口竞争 |
| **本地回环** | 只绑定 `127.0.0.1`，不监听外网 | 安全：渲染器不做 TLS，必须限制为本地访问 |
| **JSON 文本帧** | 仅用 WebSocket Text Frame + UTF-8 JSON，不用 Binary | 跨语言、可调试、渲染器侧已固化此契约 |
| **单进程多实例** | 一个控制面板进程管理多个渲染器进程，每实例一条 WS 连接 | 用户可能同时养多只宠物 |
| **令牌认证** | 每个渲染器实例启动时分配 256-bit 随机令牌，WS 握手时校验 | 防止本机其他进程或浏览器页面注入指令 |
| **子进程隔离** | 渲染器是独立可执行文件，通过 CLI 参数接收配置 | 渲染器崩溃不能拖垮控制面板；分离升级 |

### 2.3 参考实现的规模（仅供工作量估算）

JavaFX 参考实现的代码规模（`controller/src/main/java/com/desktoppet/`）：

| 层 | 文件数 | 约行数 | 说明 |
|:---|:---:|:---:|:---|
| UI | 5 | ~3300 | 1 个主控制器（2659 行）+ 托盘 + 2 个子页 + 1 个监控数据模型 |
| Core | 15 | ~1500 | 配置管理（5）、调度、交互、行为引擎、监控采集、扫描解析（4）、状态、存根 |
| Network | 3 | ~400 | Protocol + WebSocketServer + MessageDispatcher |
| Util | 2 | ~440 | 进程管理 + 开机自启 |
| Model | 24 | ~1200 | 23 个 Record + 1 个可观察 Bean |
| **合计** | **49** | **~6900** | — |

---

## 三、与渲染引擎的接口契约

> **强制性**：无论用什么技术栈实现控制面板，本节的连接参数、消息格式、认证流程、命令/事件清单**必须完全一致**，否则无法与现有 C++ 渲染器互通。完整规范见 [通信协议](../protocol/README.md)、[Commands](../protocol/commands.md)、[Events](../protocol/events.md)、[握手流程](../protocol/handshake.md)、[错误码](../protocol/error-codes.md)。

### 3.1 连接参数

| 参数 | 值 |
|:---|:---|
| 协议 | `ws://`（无 TLS） |
| 绑定地址 | `127.0.0.1`（本地回环，**禁止 0.0.0.0**） |
| 端口 | **9001** |
| 帧类型 | Text Frame（UTF-8） |
| 序列化 | JSON |
| Ping 间隔 | 45 秒（由渲染器发起，库通常自动回复 Pong） |
| 最大重连间隔 | 30 秒（渲染器侧重连，控制面板无需干预） |
| 连接数 | 每实例一条；同一实例新连接替换旧连接（Close Code 1000） |

### 3.2 Envelope 消息格式

所有消息共享统一信封：

```json
{
  "type": "command",
  "action": "play_motion",
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "payload": { "group": "Idle", "index": 0, "priority": 1 },
  "timestamp": 1710000000000
}
```

| 字段 | 类型 | 必填 | 说明 |
|:---|:---|:---:|:---|
| `type` | string | ✓ | `"command"` / `"event"` / `"response"` |
| `action` | string | ✓ | 操作名（如 `"load_model"`、`"hit"`） |
| `id` | string | ✓ | 唯一标识，用于 request-response 匹配。任意非空字符串，建议 ≤ 64 字符 |
| `payload` | object | ✓ | 消息负载。**始终存在**，空时为 `{}`，**绝不为 null** |
| `timestamp` | number (int64) | ✓ | Unix 毫秒时间戳 |

> **反序列化规则**：缺任一必填字段 → 视为无效消息，**静默丢弃**（不抛异常）。JSON 格式错误同样丢弃。

#### Response Envelope 的关键约定

`success`、`error_code`、`error_message` 三个字段位于 **JSON 顶层**（与 `type`/`action` 平级），**不在 `payload` 内**。Response 的 `payload` **始终为 `{}`**。

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

- 序列化规则：仅当 `type == "response"` 时写入这三个字段；command/event 的 JSON **不包含**它们。
- 反序列化规则：非 Response 消息将这三个字段视为不存在。
- `id` 复用原始 Command 的 `id` 用于匹配。建议 pending request 设置 **10 秒超时**。

### 3.3 认证与握手流程

```
控制面板                                       渲染器
   │                                            │
   │ ① 生成 256-bit 随机令牌（SecureRandom）      │
   │ ② 注册令牌：<instanceId, token>             │
   │ ③ 启动子进程，CLI 传参：                     │
   │   --port 9001 --instance-id N --token <hex> │
   │ ────────────────────────────────────────►   │ (进程创建)
   │                                            │
   │           ws://127.0.0.1:9001/?instance_id=N&token=<hex>
   │ ◄──────────── TCP + WS 升级 ──────────────── │
   │                                            │
   │ ④ 三道闸门校验：                             │
   │    a. Origin 守卫（拒绝 http:// https://）   │
   │    b. instance_id 存在且为整数               │
   │    c. token 与注册值匹配                     │
   │    失败分别关闭：4001 / 4000 / 4002          │
   │                                            │
   │ ◄───── event: ready ──────────────────────── │
   │       {version:"1.0.0", capabilities:["live2d"]} │
   │                                            │
   │ ⑤ 收到 ready 后，发送 9 条启动指令齐射        │
   │    （load_model, set_position, set_size,    │
   │     set_opacity, set_fps, set_volume,       │
   │     set_layout, set_subtitle_layout,        │
   │     set_subtitle_style）                    │
   │ ─────────────────────────────────────────►  │
   │                                            │
   │ ◄───── event: model_loaded ───────────────── │
   │ ───── command: set_hit_areas ─────────────► │
   │                                            │
   │ ═══════ 稳态运行 ═══════                     │
```

**关键约束**：
- 控制面板**必须等待 `ready` 事件**后才能下发指令；`ready` 之前发送的指令不保证处理。
- 令牌必须在渲染器进程启动**之前**注册，避免竞态。
- 令牌格式：32 字节随机数的十六进制（64 字符小写）。
- Origin 守卫：拒绝所有 `Origin` 以 `http://` 或 `https://` 开头的连接（防 DNS 重绑定与浏览器注入）。

### 3.4 渲染器 CLI 参数

控制面板启动渲染器子进程时，必须传入以下参数（工作目录设为渲染器可执行文件所在目录，确保 `Resources/` 路径正确解析）：

| 参数 | 必填 | 说明 |
|:---|:---:|:---|
| `--port <N>` | ✓ | WebSocket 端口（9001） |
| `--instance-id <N>` | ✓ | 实例 ID（整数） |
| `--token <hex>` | ✓ | 认证令牌（64 字符 hex） |
| `--model <name>` | 否 | 初始模型短名称 |
| `--x <N>` `--y <N>` | 否 | 窗口初始位置 |
| `--width <N>` `--height <N>` | 否 | 窗口初始尺寸 |

---

## 四、功能清单（架构无关）

本节穷举控制面板**必须或应当实现**的功能。每项标注【必须】/【推荐】/【可选】。

### 4.1 用户界面功能

#### 4.1.1 主窗口【必须】
- **无边框自定义窗口**：自绘标题栏（拖拽移动、最小化、关闭按钮）；支持 8 方向边缘拉伸（上/下/左/右/四角）。
- **页面切换器**：在同一窗口内通过可见性切换（而非多窗口）呈现：欢迎页 / 实例详情页 / 应用设置页 / 资源监视页。
- **标题栏快捷入口**：主题切换下拉框、设置按钮、监视按钮、最小化、关闭。
- **关闭行为可配**：直接退出 / 隐藏到系统托盘（默认）；可选启用退出确认对话框。

#### 4.1.2 实例侧边栏【必须】
- 展示所有宠物实例卡片（标签、模型名、连接状态）。
- "添加实例"按钮：弹出输入对话框获取标签 → 创建实例 → 启动渲染器。
- 点击卡片切换当前选中实例，详情页同步刷新。
- 实例创建流程：生成 UUID → 写配置文件 → 加入面板实例列表 → 持久化面板状态 → 启动。

#### 4.1.3 实例详情页【必须】
- **详情头部**：可编辑标签、连接状态徽章、启动/停止切换、重启、删除（删除需二次确认，默认聚焦"取消"）。
- **模型卡片**：模型选择下拉框（扫描 `Resources/Models/` 下所有合法目录）、语音包选择下拉框。
- **动作网格**：动态构建按钮，每个按钮显示动作组名 + 数量；点击触发对应动作。
- **表情行**：动态构建表情切换按钮。
- **参数面板**（分组）：
  - 窗口：透明度滑块（0.1–1.0）、X/Y 坐标输入框
  - 行为：拖拽模式（直接/物理，分段选择）、闲时间隔滑块（1–60 秒）
  - 渲染：图形后端（OpenGL/Vulkan，分段选择）、帧率模式（自适应/固定，分段选择 + 15–120 滑块）
  - 音频：音量滑块（0.0–1.0）、静音复选框
  - 启动：该实例开机自启开关
  - 字幕：字幕调整模式开关、字幕样式预设下拉框（15 种）
- **日志面板**：滚动列表，最多保留 50 条；带"清空"按钮。

#### 4.1.4 应用设置页【必须】
- **主题选择**：7 套主题的可视化色板卡片网格（深紫梦幻、樱花浅粉、赛博霓虹、暖橘小窝、深海蔚蓝、终端黑客、云石浅灰）。
- **界面调整**：字号滑块（11–18 px）、面板透明度滑块（0.3–1.0）。
- **启动与退出**：系统级开机自启开关、启动时最小化到托盘开关、关闭行为（直接退出/隐藏到托盘，分段选择）、退出确认开关。

#### 4.1.5 资源监视页【推荐】
- 双列折线图：左列控制面板 JVM（CPU、RSS、堆已用、堆最大），右列渲染器（CPU、RSS、GPU%、显存）。
- 轮询间隔 2 秒；超过 10 秒无数据标记"数据陈旧"。
- 每图最多保留 60 个数据点（环形缓冲）。
- GPU/显存字段在部分平台（如 Linux 无 DXGI）可能为 null，UI 应显示"—"。

#### 4.1.6 系统托盘【推荐，平台支持时】
- 托盘图标（双击切换窗口可见性）。
- 右键菜单：显示/隐藏、设置、退出。
- 关闭窗口时若配置为"最小化到托盘"：隐藏窗口但不退出进程（需禁用框架的"关闭即退出"默认行为）。

#### 4.1.7 欢迎页【推荐】
- 无实例时全屏显示：图标、标题、副标题、"创建第一个实例"按钮。
- **环境检测面板**【推荐】：检测并展示以下项的就绪状态（● 已就绪 / ● 未找到）：
  1. OpenGL 渲染器可执行文件
  2. Vulkan 渲染器可执行文件
  3. Java 运行时（移植到其他语言时替换为对应运行时）
  4. Cubism SDK / 模型资源目录
  5. 可用模型数量
  6. WebSocket 端口可用性

#### 4.1.8 主题系统【推荐】
- 至少 7 套主题；运行时切换；当前主题持久化。
- 推荐实现：基础样式表 + 主题覆盖样式表（叠加），便于扩展。
- 主题切换需在"设置页色板"和"标题栏下拉框"两处入口保持同步。

### 4.2 实例管理

- **多实例并行**【必须】：每实例独立渲染器进程、独立 WS 连接、独立配置、独立调度器。
- **实例 ID**【必须】：进程内使用整数 ID（自增），持久化使用 UUID 字符串。
- **实例隔离**【必须】：一实例崩溃不影响其他实例；调度器、消息分发器、交互处理器均按实例独立持有。
- **实例生命周期**【必须】：创建 → 启动 → 稳态 → 停止/重启/删除。
- **删除保护**【推荐】：删除实例弹出确认对话框，默认聚焦"取消"以防误删。

### 4.3 模型与语音包管理

#### 4.3.1 模型扫描【必须】
- 扫描 `<rendererDir>/Resources/Models/`，判定合法模型：目录存在 + 同名 `.model3.json` 存在。
- 返回排序后的模型短名称列表。
- **模型能力发现**【必须】：解析 `.model3.json` 提取：
  - 动作组：`FileReferences.Motions` 的所有 key + 每组数组长度
  - 表情：`FileReferences.Expressions[*].Name`
  - HitArea：`HitAreas[*].Name`
- ⚠️ **关键约束**：渲染器 `model_loaded` 事件中的 `motions`/`expressions` 字段**始终为空数组**，控制面板**必须自行解析**模型文件。

#### 4.3.2 语音包扫描【可选，Phase 3a】
- 扫描 `<rendererDir>/Resources/VoicePacks/`，判定合法语音包：目录存在 + 含 `meta.mko`。
- 解析 `meta.mko`（Protobuf 格式）提取：displayName、code、动作分组（groups）、行为模块（modules）、每条 action 的 motion/audio/lipSync/doc/fadeIn/fadeOut。

#### 4.3.3 挂载管理【可选，Phase 3a】
- 语音包与模型**解耦挂载**：同一语音包可挂载到不同模型；挂载关系持久化。
- 挂载后，点击交互优先走语音包行为引擎（`play_motion_ext`），未挂载或无对应组时回退到基础映射（`play_motion`）。

### 4.4 运行时决策

#### 4.4.1 闲时动作调度【必须】
- 定时从模型的 idle 动作池中随机选择一个，下发 `play_motion` 指令。
- 间隔可配（1–60 秒），运行时可修改。
- 支持暂停/恢复（断连时暂停，重连后恢复）。
- 非闲时动作结束（`motion_finished`）后应立即触发下一次闲时判定（避免长时间静止）。

#### 4.4.2 点击交互响应【必须】
- 收到渲染器 `hit` 事件后，按以下优先级决策：
  1. 若已挂载语音包且语音包定义了该区域的 group → 走 `play_motion_ext`（扩展动作 + 可选音频/字幕）
  2. 否则 → 查模型配置的 HitArea 映射 → 下发 `play_motion`
  3. 默认映射：`head → TapHead`、`body → TapBody`（优先级 2）
- HitArea 名称大小写容错：渲染器发送小写（`head`/`body`），配置可能用 PascalCase（`Head`/`Body`）。

#### 4.4.3 拖拽位置持久化【必须】
- 收到 `drag_end` 事件后，提取 `window_x`/`window_y`，持久化到实例配置。
- 下次启动时通过 `set_position` 恢复。

#### 4.4.4 用户布局同步【推荐】
- 渲染器侧用户交互（Shift+拖拽模型、Shift+滚轮缩放）产生 `layout_changed` 事件 → 持久化 `offset_x`/`offset_y`/`scale`。
- 窗口缩放（Ctrl+滚轮）产生 `window_resized` 事件 → 持久化窗口尺寸与位置。

### 4.5 配置与持久化

#### 4.5.1 配置目录约定【必须】
所有配置位于用户主目录下：

| 平台 | 路径 |
|:---|:---|
| Linux | `~/.config/desktop-pet/` |
| Windows | `%USERPROFILE%\.config\desktop-pet\`（参考实现沿用 XDG 风格，非 `%APPDATA%`） |

> 跨架构实现可选择遵循 XDG（Linux）/ `%APPDATA%`（Windows）规范，但需在文档中说明与参考实现的差异。

#### 4.5.2 配置文件清单【必须】

| 文件 | 作用 | 格式 |
|:---|:---|:---|
| `panel.json` | 面板窗口状态 + 实例 ID 列表 + 主题/字号/启动退出行为 | JSON，snake_case 键 |
| `instances/<uuid>.json` | 单个实例的全部配置（每实例一文件） | JSON |
| `config.json` | 全局遗留配置（向后兼容） | JSON |
| `mount.json` | 模型 ↔ 语音包挂载映射 | JSON |
| `hit_area_cache.json` | 模型 HitArea 列表磁盘缓存（避免重复解析） | JSON |

#### 4.5.3 配置健壮性要求【必须】
- 首次运行：自动创建默认配置。
- JSON 损坏：**降级为默认值，不崩溃**。
- 字段缺失：从默认值合并补齐。
- 写入：建议原子写（写临时文件 → 重命名）避免半写损坏。
- 键名约定：JSON 使用 `snake_case`（如 `position_x`、`current_model_name`）。

### 4.6 进程与生命周期管理

#### 4.6.1 启动流程【必须】
控制面板自身启动顺序：
1. 加载面板配置（`panel.json`）
2. 启动 WebSocket 服务端（绑定 127.0.0.1:9001）
3. 恢复 UI 状态（窗口位置/尺寸/主题）
4. 恢复所有实例配置（`instances/*.json`）
5. 启动标记了 `autoStart` 的实例
6. 初始化系统托盘
7. 根据配置决定是否启动时最小化到托盘

#### 4.6.2 实例启动流程【必须】
1. 解析渲染器路径（根据 `graphicsBackend` 选择 `desktop-pet-renderer` 或 `desktop-pet-renderer-vulkan`）
2. 创建 `ProcessManager`，生成认证令牌
3. 在 WS 服务端注册令牌
4. 以子进程启动渲染器（传入 CLI 参数）
5. 创建该实例的 `MessageDispatcher`，注册所有事件处理器
6. 等待 `ready` 事件
7. 收到 `ready` 后发送 9 条启动指令齐射
8. 等待 `model_loaded`，发送 `set_hit_areas`，启动闲时调度器

#### 4.6.3 崩溃恢复【必须】
- 监听渲染器进程退出（`onExit` 回调）。
- 非预期退出（非用户主动停止）→ 触发重启。
- **指数退避**：重启间隔序列 `[2s, 4s, 8s, 16s, 30s]`，**最多 5 次**。
- 超过最大次数后放弃，标记实例为错误状态。
- 需维护 `manuallyStopping` 标志集合，区分"用户主动停止"与"崩溃"。

#### 4.6.4 优雅停止【必须】
停止单个实例的三阶段：
1. 通过 WS 发送 `shutdown` 指令
2. 等待进程退出（轮询，建议 100ms 间隔，最长 5 秒）
3. 超时则强制终止（`destroyForcibly` / `taskkill /F` / `kill -9`）
4. 清理：移除令牌注册、关闭该实例 WS 连接、停止调度器、保存配置

#### 4.6.5 全局关闭【必须】
控制面板退出顺序：
1. 保存面板状态
2. 停止资源监控轮询
3. 异步停止所有实例（上述三阶段）
4. 关闭所有调度器、分发器、重启执行器
5. 停止 WebSocket 服务端
6. 移除系统托盘图标
7. 退出进程

### 4.7 系统集成

#### 4.7.1 开机自启【推荐】
- **Windows**：写注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，值名 `DesktopPet`，通过 `reg.exe` 子进程读写。
- **Linux**：写 `~/.config/autostart/desktop-pet.desktop` 文件（`Type=Application`）。
- 提供 `isEnabled()` / `enable()` / `disable()` 三个接口，运行时按 `os.name` 分发。
- 无 macOS 支持（参考实现未实现，可自行扩展为 LaunchAgent）。

#### 4.7.2 多平台支持【必须，至少 Windows + Linux】
- 渲染器路径解析需处理平台差异（Windows 可执行文件加 `.exe` 后缀）。
- 路径分隔符兼容（`/` vs `\`）。
- 端口冲突检测（启动 WS 前判断 9001 是否被占用）。

### 4.8 资源监控【推荐】

#### 4.8.1 控制面板自采集
- 采集自身进程的 CPU%、RSS、堆使用。
- 采集频率 2 秒；通过系统 API（Java 用 OSHI，其他栈选对应库）。
- **不抛异常原则**：任何采集失败将字段置零，但 `timestamp` 始终可用。
- 注意 CPU% 需要两次采样差值计算（首次返回 0）。

#### 4.8.2 渲染器采集
- 通过 WS 主动发送 `get_stats` 指令（频率 2 秒）。
- 渲染器通过 `stats_state` **事件**回传（不走 Response，因 Response 的 payload 固定为 `{}`）。
- GPU/显存字段在部分平台为 null，UI 需优雅降级显示。

---

## 五、数据模型

本节列出控制面板需管理的所有数据结构。字段名采用 **camelCase**（与 Java Records 一致）；持久化时转 `snake_case`。其他语言实现可自由选择命名风格，但持久化的 JSON 键名**建议保持 snake_case 以兼容现有配置文件**。

### 5.1 实例配置（InstanceConfig）

单只宠物的全部配置，持久化到 `instances/<uuid>.json`。**这是最核心的数据结构。**

| 字段 | 类型 | 默认值 | 说明 |
|:---|:---|:---|:---|
| `id` | string (UUID) | 新生成 | 持久化主键 |
| `label` | string | "新实例" | 用户可见标签 |
| `rendererPath` | string | — | 渲染器可执行文件绝对路径 |
| `graphicsBackend` | string | "opengl" | `"opengl"` / `"vulkan"` |
| `modelName` | string | "Hiyori" | 当前模型短名称 |
| `modelScale` | double | 1.0 | 模型缩放 |
| `windowX`, `windowY` | int | 1200, 600 | 窗口位置 |
| `windowWidth`, `windowHeight` | int | 400, 500 | 窗口尺寸 |
| `opacity` | double | 1.0 | 窗口透明度（0.0–1.0） |
| `dragMode` | string | "direct" | `"direct"` / `"physics"` |
| `idleInterval` | int | 10 | 闲时动作间隔（秒） |
| `targetFps` | int | 0 | 0=自适应；15–120=固定 |
| `autoStart` | boolean | false | 该实例是否随面板启动 |
| `currentExpression` | string | "F01" | 当前表情 |
| `voicePack` | string? | null | 挂载的语音包名（null=未挂载） |
| `volume` | double | 1.0 | 音量（0.0–1.0） |
| `muted` | boolean | false | 是否静音 |
| `layoutOffsetX`, `layoutOffsetY` | double | 0.0 | 模型布局偏移 |
| `layoutScale` | double | 1.0 | 模型布局缩放 |
| `subtitleOffsetX`, `subtitleOffsetY` | double | 0.0 | 字幕偏移 |
| `subtitleAreaWidth`, `subtitleAreaHeight` | int | 0 | 字幕区域（0=自动） |
| `subtitleFontSize` | double | 48.0 | 字幕字号 |
| `subtitleStylePreset` | string | "默认" | 字幕样式预设名 |

### 5.2 面板配置（PanelConfig）

控制面板自身的 UI 状态，持久化到 `panel.json`。

| 字段 | 类型 | 默认值 | 说明 |
|:---|:---|:---|:---|
| `panelX`, `panelY` | double | -1, -1 | 面板窗口位置（-1=首次居中） |
| `panelWidth`, `panelHeight` | double | 1200, 760 | 面板窗口尺寸 |
| `theme` | string | "深紫梦幻" | 当前主题名 |
| `fontSize` | int | 13 | 字号 |
| `panelOpacity` | double | 1.0 | 面板透明度 |
| `instanceIds` | string[] | [] | 实例 UUID 列表（顺序即侧边栏顺序） |
| `autoLaunchSystem` | boolean | false | 系统级开机自启 |
| `startMinimized` | boolean | false | 启动时最小化到托盘 |
| `closeAction` | string | "exit" | `"exit"` / `"hide_to_tray"` |
| `confirmOnExit` | boolean | false | 退出前确认 |

### 5.3 挂载配置（MountConfig）

模型与语音包的挂载关系，持久化到 `mount.json`。

```json
{
  "mounts": {
    "Hiyori": { "voice_pack": "VoicePackA" },
    "AnotherModel": { "voice_pack": null }
  }
}
```

### 5.4 模型元数据（ModelInfo）

从 `.model3.json` 解析，**不持久化**（每次模型加载时重新解析；HitArea 部分可缓存到 `hit_area_cache.json`）。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `motionGroups` | Map&lt;string, int&gt; | 组名 → 动作数量 |
| `expressions` | string[] | 表情名列表 |
| `hitAreas` | string[] | HitArea 名列表 |

### 5.5 语音包元数据（VoicePackInfo）

从 `meta.mko`（Protobuf）解析，**不持久化**（挂载时解析）。

| 字段 | 类型 | 说明 |
|:---|:---|:---|
| `dirName` | string | 目录名 |
| `displayName` | string | 显示名 |
| `code` | string | 唯一代码 |
| `basePath` | Path | 语音包根目录绝对路径 |
| `groups` | Map&lt;string, VoicePackGroup&gt; | 组代码 → 组（含 actions 列表） |
| `modules` | VoicePackModule[] | 行为模块列表 |

每条 `VoicePackAction`：`id, motionPath, audioPath, lipSyncPath, doc, fadeInMs, fadeOutMs`。

### 5.6 字幕样式（SubtitleStyle）

15 个字段的字幕样式定义。**颜色格式约定**：`RRGGBBTT`（`TT`=透明度，`0x00`=不透明，`0xFF`=全透明），渲染器内部转 ASS 的 `AABBGGRR`。

| 字段 | 默认值 | 说明 |
|:---|:---|:---|
| `fontName` | "Microsoft YaHei" | 字体 |
| `fontSize` | 48.0 | 字号 |
| `primaryColor` | `0x00FFFFFF` | 文字色（不透明白） |
| `outlineColor` | `0x00111111` | 描边色 |
| `outlineWidth` | 1.8 | 描边宽度 |
| `shadowColor` | `0x00000000` | 阴影色 |
| `shadowDepth` | 0.0 | 阴影深度（0=无） |
| `alignment` | 2 | ASS 数字键盘对齐（2=中下） |
| `marginV` | 30 | 垂直边距 |
| `edgeBlur` | 0.6 | 边缘模糊 |
| `fontWeight` | -1 | 字重（-1=不注入） |
| `letterSpacing` | 0.5 | 字间距 |
| `bgBoxEnabled` | false | 是否启用背景框 |
| `bgBoxColor` | `0x80000000` | 背景框色（50% 透明黑） |
| `bgBoxPaddingX`, `bgBoxPaddingY` | 12.0, 6.0 | 背景框内边距 |

**15 种字幕预设**：默认、阴影、气泡框、极简、樱花粉、赛博霓虹、星空紫、橙焰活力、和风墨韵、极简投影、流媒体盒、毛玻璃、终端绿、暗夜卡片、消息气泡。

### 5.7 运行时统计

| 模型 | 来源 | 字段 |
|:---|:---|:---|
| `ControllerStats` | 本地采集 | `cpuPercent, rssBytes, heapUsedBytes, heapMaxBytes, timestampMs` |
| `RendererStats` | WS `stats_state` 事件 | `cpuPercent, rssBytes, gpuPercent?, gpuName?, vramUsedBytes?, vramTotalBytes?, timestampMs`（`?`=可空） |

### 5.8 运行时状态（PetState）

> ⚠️ **参考实现说明**：参考实现中定义了 `PetStateManager`，但**实际未接入生产代码**——运行时状态实际通过可观察的 UI 模型（如 JavaFX Properties）在各组件间流转。新实现可选择两种方案之一：
> - **方案 A**：显式的线程安全状态管理器（读写锁保护不可变快照）
> - **方案 B**：可观察属性直接绑定 UI（参考实现的实际做法）

状态字段：`currentModelName, windowX, windowY, windowWidth, windowHeight, connected, modelLoaded`。

---

## 六、协议命令清单（控制面板 → 渲染器）

> 完整字段定义见 [Commands](../protocol/commands.md)。下表为快速索引，标注【必须实现】/【推荐】/【可选/Phase 3】。

### 6.1 启动齐射（收到 `ready` 后立即发送，9 条）

| 命令 | Payload 关键字段 | 必须? |
|:---|:---|:---:|
| `load_model` | `model_path`（模型短名称） | ✓ |
| `set_position` | `x, y`（像素坐标） | ✓ |
| `set_size` | `width, height`（像素，钳制 100–2000） | ✓ |
| `set_opacity` | `opacity`（0.0–1.0） | ✓ |
| `set_fps` | `fps`（0=自适应；1–120=固定） | ✓ |
| `set_volume` | `volume, muted` | ✓ |
| `set_layout` | `offset_x, offset_y, scale`（仅非默认时发送） | ✓ |
| `set_subtitle_layout` | `offset_x, offset_y, area_width, area_height, font_size` | ✓ |
| `set_subtitle_style` | 完整 15 字段样式块 | ✓ |

### 6.2 运行时命令

| 命令 | Payload 关键字段 | 触发场景 | 回执 | 必须? |
|:---|:---|:---|:---:|:---:|
| `play_motion` | `group, index=0, priority=2` | 闲时调度、点击交互、UI 按钮 | ✗ | ✓ |
| `play_motion_ext` | `motion_path, priority, fade_in, fade_out, audio_path?, lip_sync_path?, subtitle_text?, subtitle_duration?` | 语音包行为 | ✓ | 可选(3a) |
| `play_audio` | `audio_path, volume=1.0` | 语音包纯音频 | ✓ | 可选(3b) |
| `stop_audio` | `{}` | 停止音频 | ✓ | 可选(3b) |
| `set_expression` | `expression_id` | UI 表情按钮 | ✗ | ✓ |
| `stop_motion` | `{}` | 停止动作 | ✗ | 推荐 |
| `set_hit_areas` | `hit_areas: string[]` | `model_loaded` 后 | ✓ | ✓ |
| `get_layout` | `{}` | 查询布局（响应走 `layout_state` 事件） | ✗ | 推荐 |
| `reset_layout` | `{}` | 重置布局 | ✓ | 推荐 |
| `get_stats` | `{}` | 资源监控轮询（响应走 `stats_state` 事件） | ✗ | 推荐 |
| `show_subtitle` | `text, duration, + 样式字段` | 字幕显示 | ✓ | 推荐 |
| `hide_subtitle` | `{}` | 字幕隐藏 | ✓ | 推荐 |
| `set_subtitle_adjust_mode` | `enabled` | 字幕调整模式 | ✓ | 推荐 |
| `shutdown` | `{}` | 优雅关闭 | ✓ | ✓ |
| `set_scale` | `scale` | ⚠️ **渲染器侧为 stub**，用 `set_layout` 替代 | ✗ | 不推荐 |

### 6.3 动作优先级常量

| 常量 | 值 | 用途 |
|:---|:---:|:---|
| `PriorityNone` | 0 | 无优先级 |
| `PriorityIdle` | 1 | 闲时动作（可被任何动作中断） |
| `PriorityNormal` | 2 | 普通动作（点击触发） |
| `PriorityForce` | 3 | 强制动作（不可被中断） |

---

## 七、协议事件清单（渲染器 → 控制面板）

> 完整字段定义见 [Events](../protocol/events.md)。

| 事件 | Payload 关键字段 | 控制面板响应 | 必须? |
|:---|:---|:---|:---:|
| `ready` | `version, capabilities` | 发送 9 条启动齐射 | ✓ |
| `model_loaded` | `model_id, motions:[], expressions:[]` | 解析模型文件 → 发 `set_hit_areas` → 初始化语音包引擎 → 启动调度器 | ✓ |
| `model_load_failed` | `error_code, error_message` | 记录日志 | ✓ |
| `motion_started` | `group, index` 或 `motion_path` | 记录日志 | 推荐 |
| `motion_finished` | 同上 | 记录日志；非闲时动作结束后触发闲时判定 | ✓ |
| `hit` | `area_id, x, y, button` | 查映射 → 发 `play_motion` 或 `play_motion_ext` | ✓ |
| `drag_start` | `x, y` | 记录拖拽状态 | 推荐 |
| `drag_end` | `x, y, window_x, window_y` | 持久化窗口位置 | ✓ |
| `layout_changed` | `offset_x, offset_y, scale` | 持久化模型布局 | 推荐 |
| `window_resized` | `window_width, window_height, window_x, window_y` | 持久化窗口尺寸+位置 | 推荐 |
| `stats_state` | `cpu_percent, rss_bytes, gpu_percent?, ...` | 更新监视页 | 推荐 |
| `error` | `error_code, error_message` | 记录日志 | ✓ |
| `layout_state` | `offset_x, offset_y, scale` | `get_layout` 的响应（按 action 路由，**不按 id 匹配**） | 推荐 |

### 事件路由规则

```
收到消息
   │
   ├─ type == "response" → 按 id 查找 pending request，完成 CompletableFuture
   │                        （参考实现中此路径未实际使用，响应仅记录日志）
   │
   └─ type == "event"    → 按 action 字符串查找已注册的处理器
                            未注册 → 记录 WARN 日志，不抛异常
                            处理器异常 → 隔离，不影响其他事件
```

> **重要**：`stats_state`、`layout_state` 这类"伪响应"事件，其 `id` 由渲染器新建，**不复用**原始请求的 `id`。控制面板**必须按 action 路由，不能按 id 匹配 pending request**。

### 线程安全约束

- 事件到达在 WebSocket 库的 I/O 线程上。
- 事件处理器若修改 UI 状态，**必须切换到 UI 线程**（JavaFX 用 `Platform.runLater`，其他框架用对应机制）。
- 事件处理器异常必须被隔离，不得传播到消息泵导致后续消息丢失。

---

## 八、关键行为规格

### 8.1 实例启动时序（精确步骤）

```
1. 解析渲染器路径（backend-aware）
2. new ProcessManager(rendererPath, 9001)
3. wsServer.registerToken(instanceId, pm.getAuthToken())
4. pm.startRenderer(instanceId, modelName, x, y, width, height)
5. new MessageDispatcher() → 存入 dispatchers[id]
6. registerInstanceEventHandlers(instance, dispatcher)
   （注册 ready/model_loaded/hit/drag_end/... 等 13 个事件处理器）
7. pm.setExitCallback(exitCode -> handleUnexpectedExit(...))
8. 等待 ready 事件（异步，由 dispatcher 路由）
9. [ready 到达] 发送 9 条启动齐射
10. [model_loaded 到达] 
    a. 解析模型文件获取 motionGroups/expressions/hitAreas
    b. 缓存 hitAreas 到磁盘
    c. 发送 set_hit_areas
    d. 加载该模型的 model_config.json（如存在）
    e. 初始化语音包行为引擎（如已挂载）
    f. 启动闲时调度器
11. 实例进入稳态
```

### 8.2 崩溃恢复规格

```
渲染器进程退出
   │
   ├─ 在 manuallyStopping 集合中？ → 是：正常停止，清理资源
   │
   └─ 不在 → 视为崩溃
        │
        ├─ restartAttempts[id] >= 5？ → 是：标记错误，放弃
        │
        └─ 否：
             │
             ├─ 计算延迟 = BACKOFF[min(attempts, 4)]
             │   BACKOFF = [2s, 4s, 8s, 16s, 30s]
             │
             ├─ 调度延迟任务 → 重启实例（回到 8.1 步骤 2）
             │
             └─ restartAttempts[id]++
```

### 8.3 闲时调度规格

```
调度器启动
   │
   ├─ 周期 = idleInterval * 1000 ms
   ├─ 动作池 = 模型的 Idle 组 motions
   ├─ 回调 = (motionName) → 发送 play_motion
   │
   ▼
每个周期：
   │
   ├─ paused？ → 跳过本轮
   ├─ 动作池为空？ → 跳过本轮
   │
   └─ 随机选一个 → 发送 play_motion(group=Idle, index=N, priority=1)
```

**关键 API 区分**：
- `pause()` / `resume()`：仅切换暂停标志，调度器继续运行但不触发回调。
- `triggerNow()`：**立即触发一次**回调 + 重置周期定时器。用于非闲时动作结束后的快速衔接。
- `updateInterval(ms)`：运行时修改周期，重新调度。
- `setIdleMotions(list)`：运行时替换动作池。

### 8.4 点击交互决策规格

```
收到 hit 事件 { area_id }
   │
   ├─ 已挂载语音包 且 语音包定义了 area_id 对应的 group？
   │   │
   │   ├─ 是：从该 group 的 actions 中随机选一条
   │   │      │
   │   │      ├─ 构造 play_motion_ext（绝对路径 + priority + fade + audio_path + ...）
   │   │      ├─ 暂停闲时调度器
   │   │      └─ 发送
   │   │
   │   └─ 否：回退 ↓
   │
   └─ 基础映射（InteractionHandler）
        │
        ├─ 查模型配置的 hitActions[area_id]
        ├─ 容错：hitActions[capitalize(area_id)]
        ├─ 默认：{ head: TapHead, body: TapBody }（priority=2）
        │
        └─ 构造 play_motion(group, index=0, priority) → 发送
```

### 8.5 状态同步策略

控制面板与渲染器之间是**事件驱动**的状态同步，而非请求-响应轮询：

| 场景 | 同步方向 | 机制 |
|:---|:---|:---|
| 控制面板启动 | 面板 → 渲染器 | `ready` 后的 9 条指令齐射 |
| 模型切换 | 面板 → 渲染器 | `load_model` → 等待 `model_loaded` |
| 用户调参 | 面板 → 渲染器 | 立即发送对应指令（实时生效） |
| 用户拖拽宠物 | 渲染器 → 面板 | `drag_end` 事件携带最终坐标 |
| 用户调整布局 | 渲染器 → 面板 | `layout_changed` 事件 |
| 用户缩放窗口 | 渲染器 → 面板 | `window_resized` 事件 |
| 资源监控 | 面板 → 渲染器 → 面板 | `get_stats` 轮询 → `stats_state` 事件回传 |

> **关于 request-response**：参考实现虽定义了 `CompletableFuture<Envelope>` 关联机制（按 id 匹配 pending request），但**当前生产代码未实际调用**——所有命令都是 fire-and-forget，依赖事件流确认状态变迁（如 `load_model` 后等 `model_loaded`，而非等 Response）。新实现可选择更严格的 request-response 语义，但对 16 条标注"需要 Response"的命令应支持 Response 处理。

---

## 九、跨架构实现建议

### 9.1 技术栈选择考量

| 考量维度 | 建议 |
|:---|:---|
| **GUI 框架** | 需支持：无边框窗口、自定义绘图（标题栏/托盘）、CSS 或等价样式系统（主题切换）、数据绑定（实时刷新）。合格：Qt、WPF/WinUI、Electron、Tauri、Flutter Desktop、Avalonia。 |
| **WebSocket 库** | 需支持 Server 模式、Text Frame、查询参数解析、自定义关闭码。避免只支持 Client 的库。 |
| **JSON 库** | 需支持 snake_case 键名映射、nullable 字段、流式解析（处理 stats 轮询高频消息）。 |
| **子进程管理** | 需支持：异步退出监听、stdout/stderr 捕获、强制终止、工作目录设置。 |
| **系统 API** | Windows 注册表读写、Linux 文件系统操作、跨平台系统信息（CPU/内存采集）。 |

### 9.2 线程模型建议

参考实现的线程拓扑（推荐新实现遵循）：

| 线程 | 职责 | 注意事项 |
|:---|:---|:---|
| **UI 主线程** | 所有 UI 更新、用户事件处理 | **绝不在 UI 线程做网络 I/O 或阻塞操作** |
| **WebSocket I/O 线程** | 接收消息、初步解析 | 收到消息后通过事件队列转发到 UI 线程或业务线程 |
| **调度器线程**（每实例） | 闲时动作定时触发 | 守护线程；单线程执行器 |
| **监控采集线程** | 定时采集自身 + 渲染器统计 | 单线程；采集结果转发到 UI 线程更新图表 |
| **重启调度线程** | 崩溃恢复的延迟任务 | 单线程；守护线程 |
| **日志泵线程**（每实例） | 读取渲染器 stdout 转发到日志系统 | 守护线程；避免阻塞子进程输出缓冲区 |

### 9.3 可测试性模式

参考实现采用了以下模式，强烈推荐新实现沿用：

1. **工厂注入**：子进程启动通过 `ProcessBuilderFactory` 函数式接口注入，单元测试可 mock 出假的 `ProcessBuilder`，避免真正启动子进程。
2. **平台检测注入**：开机自启的平台检测（`windowsDetector`、`linuxDetector`）和路径解析（`appPathSupplier`、`linuxConfigDirSupplier`）通过 Supplier 注入，测试可模拟任意平台。
3. **路径可注入**：所有配置管理器的文件路径均可通过构造函数注入备用路径，测试使用临时目录。
4. **消息发送抽象**：交互处理器通过 `Consumer<String>` 接收消息发送回调，而非直接依赖 WS 库，测试可捕获发出的命令字符串。

### 9.4 平台特定实现要点

#### Windows
- 渲染器可执行文件名加 `.exe` 后缀。
- 开机自启：`reg.exe query/add/delete` 操作 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- 系统托盘：使用 Shell_NotifyIcon 或框架封装。
- 进程强制终止：`taskkill /F /PID <pid>` 或等价 API。
- GPU/显存采集：PDH（GPU%）+ DXGI（VRAM，per-process `QueryVideoMemoryInfo`）。

#### Linux
- 渲染器可执行文件无后缀。
- 开机自启：写 `~/.config/autostart/desktop-pet.desktop`。
- 系统托盘：遵循 [StatusNotifierItem](https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/) 规范（AppIndicator）。
- 进程强制终止：`kill -9 <pid>`。
- GPU/显存采集：可能无原生支持（参考实现为 Stub，字段返回 null）。

#### macOS（参考实现未支持，新实现可扩展）
- 开机自启：LaunchAgent plist（`~/Library/LaunchAgents/`）。
- 系统托盘：NSStatusItem。
- 路径规范建议改用 `~/Library/Application Support/desktop-pet/`。

### 9.5 错误处理哲学

参考实现贯穿以下原则，建议沿用：

| 原则 | 体现 |
|:---|:---|
| **永不崩溃** | 配置文件损坏 → 降级默认值；消息格式错 → 静默丢弃；采集失败 → 字段置零 |
| **静默降级** | GPU 监控不可用 → 显示"—"；语音包解析失败 → 回退基础映射 |
| **错误隔离** | 单个事件处理器异常不影响其他事件；单实例崩溃不影响其他实例 |
| **用户可见** | 渲染器崩溃通过 UI 状态徽章反馈；端口冲突启动时报错 |
| **日志完备** | 所有错误记录 SLF4J（或等价）日志，不使用 `printStackTrace` |

---

## 十、已知限制与注意事项

> 以下是参考实现当前的已知问题或文档/代码偏差，新实现时应注意规避或显式决策。

| # | 问题 | 影响 | 新实现建议 |
|:---:|:---|:---|:---|
| 1 | `set_scale` 指令在渲染器侧为 **stub**（仅记日志，不生效） | 模型缩放无效 | 使用 `set_layout` 的 `scale` 字段替代 |
| 2 | `model_loaded` 事件的 `motions`/`expressions` **始终为空数组** | 无法从事件获取动作/表情列表 | 必须自行解析 `.model3.json` |
| 3 | `play_motion_ext` 的 `lip_sync_path`/`subtitle_text`/`subtitle_duration` 字段在代码中发送但**协议文档未记录** | 文档/代码偏差 | 新实现可选择是否发送这些字段；渲染器当前接受但 lipSync 待 Phase 3c 实现 |
| 4 | `CompletableFuture` request-response 关联机制**已实现但未使用** | 所有命令实际 fire-and-forget | 新实现可选择严格 request-response 或保持当前事件驱动模式 |
| 5 | `PetStateManager` 定义了但**未接入生产代码** | 运行时状态实际通过 UI 可观察属性流转 | 选择显式状态管理器或 UI 绑定方案，但不要两者都做却都不用 |
| 6 | 命名不一致：`windowX`/`posX`/`positionX` 三种风格并存 | 代码可读性 | 新实现统一命名风格 |
| 7 | 配置目录用 XDG 风格（`~/.config/`）而非平台规范（Windows 的 `%APPDATA%`） | Windows 用户可能不习惯 | 可选择遵循平台规范，但需处理与参考实现的迁移 |
| 8 | `stats_state`/`layout_state` 用事件而非 Response 承载数据 | 因 Response payload 固定为 `{}` | 这是协议契约，不可改变；按 action 路由 |
| 9 | Vulkan 后端的 VRAM 数值依赖 WDDM 驱动报告，可能偏低或为 0 | 监控数值不准 | 已知限制，非回归；UI 应容忍 null/0 值 |
| 10 | 音频播放采用 fire-and-forget，无播放完成事件 | 无法精确知道音频何时结束 | 自行用 duration 计时，或通过 OGG 头估算时长 |

---

## 十一、验收清单

新实现若要宣称"与参考实现等效且互通"，应通过以下验收项：

### 11.1 协议互通（必须）
- [ ] 控制面板启动后，现有 C++ 渲染器能连接到 9001 端口
- [ ] 渲染器发送 `ready` 后，收到 9 条启动齐射
- [ ] `load_model` 能成功加载模型并收到 `model_loaded`
- [ ] `hit` 事件能触发 `play_motion` 回送
- [ ] `drag_end` 事件的窗口坐标能被正确持久化
- [ ] `get_stats` / `stats_state` 轮询闭环正常
- [ ] `shutdown` 能触发渲染器优雅退出

### 11.2 功能等效（必须）
- [ ] 能创建/启动/停止/删除多个实例
- [ ] 实例崩溃后按指数退避自动重启（最多 5 次）
- [ ] 配置持久化到磁盘，重启后恢复
- [ ] 配置文件损坏时不崩溃，降级默认值
- [ ] 闲时调度器按配置间隔触发随机动作
- [ ] 主题切换实时生效并持久化

### 11.3 系统集成（推荐）
- [ ] 系统托盘图标，双击切换窗口可见性
- [ ] 开机自启（Windows 注册表 / Linux .desktop）
- [ ] 无边框窗口 + 8 方向拉伸
- [ ] 至少 Windows + Linux 双平台

### 11.4 资源监控（推荐）
- [ ] 自身 CPU/内存/堆采集（2 秒间隔）
- [ ] 渲染器 CPU/内存/GPU/显存展示
- [ ] GPU 不可用时优雅显示"—"

---

## 十二、附录：参考实现的文件清单

供新实现对照参考（路径相对 `controller/src/main/java/com/desktoppet/`）：

| 层 | 文件 | 参考实现职责 |
|:---|:---|:---|
| UI | `ui/MainWindowController.java` (2659 行) | 生命周期中枢，管理所有实例、进程、调度器、WS、监控 |
| UI | `ui/TrayManager.java` (107 行) | AWT 系统托盘 |
| UI | `ui/SettingsPageController.java` (211 行) | 应用设置页 |
| UI | `ui/MonitorPageController.java` (203 行) | 资源监视页（8 折线图） |
| UI | `ui/MonitorDataModel.java` (127 行) | 监控数据环形缓冲（COW，60 点） |
| Core | `core/ConfigManager.java` (179 行) | 全局遗留配置（config.json） |
| Core | `core/InstanceConfigManager.java` (244 行) | 实例配置（instances/&lt;uuid&gt;.json） |
| Core | `core/PanelStateManager.java` (241 行) | 面板配置（panel.json）+ 旧格式迁移 |
| Core | `core/MountConfigManager.java` (108 行) | 挂载配置（mount.json） |
| Core | `core/HitAreaCacheManager.java` (100 行) | HitArea 缓存（hit_area_cache.json） |
| Core | `core/Scheduler.java` (150 行) | 闲时动作调度 |
| Core | `core/InteractionHandler.java` (103 行) | hit → play_motion 基础映射 |
| Core | `core/MountedBehaviorEngine.java` (246 行) | 语音包行为引擎（play_motion_ext） |
| Core | `core/ResourceStatsCollector.java` (113 行) | 自身 JVM 资源采集（OSHI） |
| Core | `core/ModelScanner.java` (87 行) | 模型目录扫描 |
| Core | `core/ModelInfoParser.java` (109 行) | .model3.json 解析 |
| Core | `core/VoicePackScanner.java` (44 行) | 语音包目录扫描 |
| Core | `core/MetaMkoParser.java` (90 行) | meta.mko (Protobuf) 解析 |
| Core | `core/PetStateManager.java` (74 行) | ⚠️ 定义但未接入生产 |
| Core | `core/audio/AudioMapping.java` (7 行) | ⚠️ Phase 3 存根 |
| Network | `network/Protocol.java` (178 行) | Envelope 序列化 + 命令工厂 |
| Network | `network/PetWebSocketServer.java` (169 行) | WS 服务端 + 多实例连接 + 令牌认证 |
| Network | `network/MessageDispatcher.java` (58 行) | 按 id/action 路由 |
| Util | `util/ProcessManager.java` (231 行) | 渲染器进程生命周期 + 令牌生成 |
| Util | `util/AutoLaunchManager.java` (206 行) | 开机自启（Win 注册表 + Linux .desktop） |
| Model | `model/*.java` (24 文件) | 23 Records + 1 JavaFX Bean |

### 外部相关文档

| 文档 | 内容 |
|:---|:---|
| [架构总览](../README.md) | 项目整体架构、技术栈、设计原则 |
| [通信协议](../protocol/README.md) | WebSocket 协议规范（权威） |
| [协议 - Commands](../protocol/commands.md) | 全部 25 条指令定义 |
| [协议 - Events](../protocol/events.md) | 全部 13 个事件定义 |
| [协议 - 握手流程](../protocol/handshake.md) | 连接建立时序图 |
| [协议 - 错误码](../protocol/error-codes.md) | 错误码体系 |
| [控制面板设计 (Java)](./README.md) | JavaFX 参考实现细节（本文档的补充） |
| [启动流程](../system/startup.md) | 详细启动/关闭流程 |
| [容错与错误处理](../system/fault-tolerance.md) | 崩溃恢复、断连处理 |
| [配置文件设计](../system/configuration.md) | 配置文件格式详解 |
| [外置语音包挂载](../system/voice-pack-mounting.md) | 语音包挂载设计 |

---

> **文档版本**：v1.0 · 基于 commit `12cef2d`（branch `refactor/decouple-opengl-renderer`）的参考实现编写。
> 如参考实现发生重大变更（新增指令、调整协议、改变启动流程等），本文档需同步更新。
