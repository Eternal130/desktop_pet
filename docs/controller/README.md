# 控制面板设计 (Java)

> **实现状态**：Phase 2（控制面板）已完成，Phase 3a（语音包挂载）双侧已完成——Java 侧扫描/解析/挂载/行为引擎，渲染器侧 `play_motion_ext` 指令已接入。Phase 2.x 渲染后端选择（`graphicsBackend`）集成、7 套主题系统、欢迎页环境检测、实例删除确认、`AutoLaunchManager` 开机自启已全部完成。Phase 3b 渲染器侧音频播放（`AudioManager`）已实现，控制器侧音频映射管理（`AudioMappingManager`）与音频管理 UI 待实现。
>
> 控制面板包括 Tab 式 UI、多实例管理、业务逻辑、进程管理、系统托盘、崩溃恢复、语音包扫描/解析/挂载、渲染后端选择、主题切换、欢迎页引导等核心功能。
>
> 本文档描述 Java 控制面板的模块设计、技术选型和闲时行为策略。
> 整体架构参见 [架构总览](../README.md)，通信协议参见 [通信协议](../protocol/README.md)，
> 工具链与版本详见 [工程化](../engineering/README.md)。

---

## 一、技术概要

| 项目 | 选型 |
|:---|:---|
| 语言 | Java 21 LTS (OpenJDK 21) |
| UI 框架 | JavaFX 21 (OpenJFX 21.0.5) — FXML 声明式布局 + CSS 样式（选择与 JDK 21 对齐的 LTS 版本） |
| 构建 | Maven ≥ 3.9 |
| WebSocket | Java-WebSocket 1.6.0（Server） |
| JSON | Gson 2.13.2 |
| 日志 | SLF4J 2.0.17 + Logback 1.5.32 |
| Protobuf | protobuf-java 4.29.3（解析 meta.mko 语音包索引） |
| 打包 | jlink + jpackage（内嵌最小化 JRE） |

---

## 二、模块概览

### 2.1 UI 层（JavaFX）

使用 JavaFX 21 构建用户界面，采用 FXML + CSS 分离布局与样式。主窗口为 **Tab 式布局**，每个 Tab 由独立 FXML + Controller 管理：

- **主窗口（MainWindowController + `main-window.fxml`）**：Tab 容器，管理 Dashboard/Settings/Actions/Advanced 四个 Tab 页的加载与切换。`App.java` 继承 `javafx.application.Application`，作为 JavaFX 生命周期入口，在 `start()` 中创建 `MainWindowController`。同时承载 **欢迎页**（`welcomePane`，由 `buildWelcomePane()` 动态构建）：无实例时全屏显示，包含图标/标题/副标题 + **环境检测面板**（检测 OpenGL 渲染器、Vulkan 渲染器、Java、Cubism Core、模型资源、WebSocket 端口是否就绪），每个检测项展示 `● 已就绪 / ● 未找到` 状态与详情
- **Dashboard Tab（DashboardTabController + `tab-dashboard.fxml`）**：宠物当前状态显示（连接状态、当前模型、动作组信息）、模型切换（ComboBox 选择可用模型）、渲染器重启按钮、运行时日志（ListView）
- **Settings Tab（SettingsTabController + `tab-settings.fxml`）**：行为配置——拖拽模式切换（ToggleGroup: direct/physics）、闲时动作间隔（Spinner）、帧率模式（自适应/固定，Spinner 15-120fps）、窗口透明度（Slider）、开机自启（CheckBox）、HitArea 映射表（TableView）、语音包选择（ComboBox）
- **Actions Tab（ActionsTabController + `tab-actions.fxml`）**：动作/表情手动触发——动作组/索引/优先级选择（ComboBox），快捷动作按钮（FlowPane），表情切换（FlowPane）
- **Advanced Tab（AdvancedTabController + `tab-advanced.fxml`）**：高级设置
- **设置页（SettingsPageController + `settings-page.fxml`）**：统一设置入口（独立 Stage）。提供 **主题卡片网格**（`themeGrid`，可视化色板选择，`theme-active` 伪类高亮当前主题）、字号 Slider、面板透明度 Slider、开机自启 CheckBox、启动时最小化 CheckBox、关闭行为单选（退出/最小化到托盘）、退出确认 CheckBox。同时保留 **设置面板**（`SettingsPanelController` + `settings-panel.fxml`，兼容旧入口）
- **主题系统（7 套）**：基础样式表 `style.css` + 6 套主题覆盖样式表（`theme-sakura.css` 樱花浅粉、`theme-cyber.css` 赛博霓虹、`theme-warm.css` 暖橘小窝、`theme-ocean.css` 深海蔚蓝、`theme-terminal.css` 终端黑客、`theme-stone.css` 云石浅灰），其中 `深紫梦幻` 对应基础 `style.css`。当前主题存于 `PanelConfig.theme`（字符串），运行时通过 `MainWindowController.applyTheme()` 切换 Scene 的 CSS。提供 **双入口切换**：设置页主题卡片网格（可视化色板）+ 主窗口状态栏主题 ComboBox（`themeCombo`），两者保持同步
- **渲染后端选择**：设置页与状态栏提供 OpenGL/Vulkan 切换按钮（`backendOpenGLBtn` / `backendVulkanBtn`，`SEG_ACTIVE` 伪类标识当前后端）。切换后写入当前实例 `InstanceConfig.graphicsBackend`，并通过后端感知的 `resolveRendererPath()` 选择对应可执行文件
- **系统托盘（TrayManager）**：使用 `java.awt.SystemTray` API（JavaFX 未提供原生托盘支持）。关闭窗口时隐藏到托盘而非退出（`Platform.setImplicitExit(false)`），双击托盘图标切换窗口可见性，右键菜单包含 显示/隐藏、设置、退出
- **宠物管理**：模型切换（ComboBox 选择模型目录）、模型导入、**实例删除**（`deleteInstance()` 弹出 `Alert.AlertType.CONFIRMATION` 确认对话框，默认聚焦 CANCEL 按钮以防误删；确认后停止渲染进程、删除实例配置文件、从列表移除并持久化面板状态）
- **语音包管理（Phase 3a ✅）**：Settings Tab 中提供语音包选择 ComboBox，挂载/卸载操作通过 `MainWindowController` 协调
- **音频管理（Phase 3b）**：音频播放由渲染器侧 `AudioManager`（miniaudio + libvorbis）完成，已接入 `play_audio`/`stop_audio`/`set_volume` 指令（错误码 7001/7002/7003）✅。控制器侧音频映射管理（`AudioMappingManager`）与音频管理 UI **待实现**——目前仅定义了 `AudioMapping` 记录（`motionGroup` + `audioPath`，标注 Phase 3 stub）

**UI 架构要点**：

| 关注点 | 方案 |
|:---|:---|
| 布局定义 | FXML 文件（`src/main/resources/fxml/`），每个 Tab 独立 FXML，通过 `FXMLLoader` 加载 |
| 样式管理 | CSS 样式表（`src/main/resources/css/`），1 份基础 `style.css` + 6 份主题覆盖 CSS，支持运行时主题切换 |
| 事件绑定 | FXML Controller 类，使用 `@FXML` 注解绑定 UI 组件 |
| 线程安全 | UI 更新必须通过 `Platform.runLater()` 回到 JavaFX Application Thread；AWT 操作（托盘）通过 `SwingUtilities.invokeLater()` |
| 可视化设计 | 推荐使用 JavaFX Scene Builder 辅助设计 FXML 布局 |
| 模块系统 | `module-info.java` 声明 JPMS 模块，`opens` 包给 `javafx.fxml`、`com.google.gson` 和 `com.google.protobuf` |
| 多实例模型 | `PetInstance` 使用 JavaFX Properties（`StringProperty`、`IntegerProperty` 等），支持 UI 双向绑定 |

### 2.2 业务逻辑层

- **主窗口控制器（MainWindowController）**：**控制面板的核心中枢**，负责启动/关闭的完整编排：配置加载 → WebSocket Server 启动 → 事件处理器注册 → 渲染器进程启动 → 握手 → 模型加载 → 位置恢复。同时负责崩溃恢复（指数退避重启）和断连处理（关键指令缓存）。支持多实例管理（每实例独立渲染器进程、独立端口、独立配置）。详见 [启动流程](../system/startup.md)
- **状态管理器（PetStateManager）**：维护宠物运行时状态。使用 `ReentrantReadWriteLock` 保证线程安全，`getState()` 返回不可变的 `PetState` 快照（Java Record）。当前不实现心情/活力等养成属性，架构预留权重打分扩展
- **交互处理器（InteractionHandler）**：接收渲染器上报的 `hit` 事件，查询模型行为映射配置，决定后续响应（构建 `play_motion` 指令并通过 `Protocol.createCommand()` 序列化发送）。内置默认映射（`head→TapHead`、`body→TapBody`），支持大小写容错（渲染器发送小写 key，配置使用 PascalCase）
- **定时任务（Scheduler）**：使用 `java.util.concurrent.ScheduledExecutorService` 实现定时闲时动作触发。支持 `pause()`/`resume()`（断连时暂停、重连后恢复）、`setIdleMotions()` 动态更新动作池、`updateInterval()` 修改触发间隔
- **配置管理器（ConfigManager）**：基于 Gson 读写 JSON 配置文件（`~/.config/desktop-pet/config.json`）。首次运行自动创建默认配置，JSON 损坏时降级为默认值（不崩溃），部分字段缺失时从 `PetConfig.defaults()` 合并。JSON 使用 `snake_case` 键名（`position_x`、`current_model_name`），Java Record 使用 `camelCase`
- **模型信息解析器（ModelInfoParser）**：直接解析 Cubism 的 `.model3.json` 文件，提取动作组（Map<String, Integer>）、表情列表、HitArea 列表。**重要**：渲染器的 `model_loaded` 事件中 motions/expressions 始终为空数组，Java 端必须自行解析模型文件获取这些信息
- **HitArea 缓存管理器（HitAreaCacheManager）**：缓存每个模型的 HitArea 列表到 `~/.config/desktop-pet/hit_area_cache.json`，避免重复解析模型文件
- **音频映射管理器（AudioMappingManager）**（Phase 3b 待实现）：当前仅定义了 `AudioMapping` 记录（`motionGroup` + `audioPath`，标注 Phase 3 stub）。完整的映射管理（增删改查、持久化）与音频管理 UI 待 Phase 3b 后续实现。注意音频**播放**本身已在渲染器侧 `AudioManager` 实现
- **开机自启管理器（AutoLaunchManager）**（`util/AutoLaunchManager.java`）：OS 特定的开机自启管理。Windows 通过 `reg` 命令读写注册表键 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`；Linux 写入 `~/.config/autostart/desktop-pet.desktop` 文件。提供 `isEnabled()` / `enable()` / `disable()` 三个接口，运行时按 `os.name` 自动分发到对应实现。采用可注入的 `ProcessBuilderFactory` + 多个 `Supplier`（appPath、linuxConfigDir、平台检测）便于单元测试 mock（同 `ProcessManager` 的可测试性模式）

### 2.3 多实例管理层

支持同时运行多个宠物实例，每个实例独立配置、独立渲染器进程：

- **PetInstance**：宠物实例的 JavaFX 可观察模型。使用 JavaFX Properties（`StringProperty`、`BooleanProperty` 等）实现 UI 双向绑定。包含 label、model、status、connected、opacity、dragMode、idleInterval、窗口位置/尺寸、voicePack、volume、graphicsBackend 等属性。内置日志缓冲（`ObservableList<String>`，最多 50 条）。提供 `fromInstanceConfig()` / `toInstanceConfig()` 与持久化配置的双向转换
- **InstanceConfig**（Java Record）：单个实例的持久化配置——id（UUID）、label、rendererPath、**graphicsBackend**（`"opengl"` / `"vulkan"`，JSON key `graphics_backend`，默认 `"opengl"`）、modelName、modelScale、窗口位置/尺寸/透明度、dragMode、idleInterval、targetFps、autoStart、currentExpression、voicePack、volume、layoutOffsetX/Y、layoutScale
- **InstanceConfigManager**：每个实例配置存储为独立 JSON 文件（`~/.config/desktop-pet/instances/{uuid}.json`），支持 save/load/delete/loadAll
- **PanelConfig**（Java Record）：面板级配置——窗口位置/尺寸、主题（`theme`，默认 `"深紫梦幻"`）、字号（`fontSize`）、面板透明度（`panelOpacity`）、实例 ID 列表（`List<String> instanceIds`），以及 **启动/退出行为字段**：`autoLaunchSystem`（系统级开机自启）、`startMinimized`（启动时最小化到托盘）、`closeAction`（`"exit"` 直接退出 / `"minimize"` 最小化到托盘，默认 `"exit"`）、`confirmOnExit`（退出前确认）
- **PanelState**（Java Record）：面板状态快照，包含实例状态列表
- **PanelStateManager**：管理 `~/.config/desktop-pet/panel.json`，支持从旧版 `panel-state.json` 自动迁移（创建迁移后的实例配置文件并备份旧文件）
- **SystemConfig**（Java Record）：系统级配置——`autoStart`、`defaultGraphicsBackend`（JSON key `default_graphics_backend`，新建实例的默认渲染后端，默认 `"opengl"`）
- **渲染器路径解析（resolveRendererPath）**：位于 `MainWindowController`，后端感知的渲染器可执行文件查找逻辑。传入 `backend` 参数（`"opengl"` / `"vulkan"`），返回对应可执行文件路径——`"vulkan"` 映射到 `desktop-pet-renderer-vulkan`（Windows 上追加 `.exe`），`"opengl"`（或其他回退值）映射到基础 `desktop-pet-renderer`。启动新实例、欢迎页环境检测、状态栏后端切换均通过此方法解析路径，确保图形后端选择在控制面板与渲染器可执行文件之间保持一致

### 2.4 语音包挂载层（Phase 3a ✅ 已实现）

实现语音包与模型的解耦挂载，Java 侧与渲染器侧核心组件均已完成：

- **VoicePackScanner**：扫描 Resources 目录，识别含 `meta.mko` 的语音包目录。返回排序后的语音包名列表
- **MetaMkoParser**：使用 protobuf-java 解析 `meta.mko` 二进制文件（`Bundle.parseFrom()`），输出 `VoicePackInfo`。提取元数据（displayName、code）、动作分组（groups）、行为模块（modules）、每个 action 的 motion/audio/lipSync/doc/fadeIn/fadeOut
- **VoicePackInfo**（Java Record）：语音包元数据模型——dirName、displayName、code、basePath、groups（`Map<String, VoicePackGroup>`）、modules（`List<VoicePackModule>`）
- **VoicePackGroup**（Java Record）：动作分组——code（事件名）、name（显示名）、priority、actions 列表
- **VoicePackAction**（Java Record）：单条动作映射——id、motionPath、audioPath、lipSyncPath、doc、fadeInMs、fadeOutMs
- **VoicePackModule**（Java Record）：行为模块——key、priority、filePath
- **MountConfig**（Java Record）：挂载配置——modelName、voicePackName（null 表示未挂载）
- **MountConfigManager**：挂载关系持久化到 `~/.config/desktop-pet/mount.json`。支持 `loadForModel()`、`saveForModel()`、`loadAll()`。JSON 损坏时降级为空配置
- **MountedBehaviorEngine**：运行时行为引擎。`hasGroupForArea(areaId)` 检查语音包是否有对应事件组，`buildMotionCommand(areaId)` 从事件组随机选择一条 action 并构建 **`play_motion_ext`** 指令（绝对路径 + priority + fadeIn/fadeOut），同时附带 audio_path（如有）。该指令 ✅ 渲染器侧已实现，可接收扩展 motion + 关联音频路径，触发动作播放并联动 `AudioManager`

> **详细设计**参见 [外置语音包挂载](../system/voice-pack-mounting.md)。

### 2.5 通信层（Network Layer）

基于 Java-WebSocket 库实现 WebSocket 服务端（控制面板为常驻进程，承担 Server 角色）：

> **注意**：以下为 Phase 1/Phase 2 实现，描述未变化。

- **PetWebSocketServer**：封装服务端启动、单连接管理（`activeConnection` 跟踪，新连接自动替换旧连接）、消息收发。使用独立线程处理网络 I/O，通过 `setMessageCallback` / `setConnectionCallback` 回调转发消息和连接状态变化
- **MessageDispatcher**：按消息 `type` 分路由——`response` 类型通过 `CompletableFuture` 匹配 `id` 完成回执（`expectResponse()` + `orTimeout()`），`event` 类型按 `action` 路由到注册的 `Consumer<Envelope>` 处理器。未注册 action 记录 WARN 日志，处理器异常隔离不传播
- **Protocol**：封装 Envelope 格式的序列化/反序列化（Gson）。**关键约定**：response 的 `success`/`error_code`/`error_message` 字段位于 JSON **顶层**（与 C++ 端保持一致），不在 `payload` 内。提供 `createCommand()`/`createEvent()`/`createResponse()` 工厂方法和 UUID 生成
- **连接管理**：监测渲染引擎连接状态，断开时通知 `MainWindowController`。详见 [容错与错误处理](../system/fault-tolerance.md)

### 2.6 进程管理器（ProcessManager）

负责渲染器进程的生命周期管理：

- 使用 `ProcessBuilder` 启动渲染器可执行文件，工作目录设为渲染器二进制所在目录（确保 `Resources/` 路径正确解析）
- 通过 `Process.onExit()` 监听进程退出，通知 `MainWindowController`。非零退出码视为崩溃，触发自动重启流程
- 停止流程：先通过回调发送 `shutdown` 指令 → 轮询等待进程退出（100ms 间隔，最长 5 秒）→ 超时后 `Process.destroyForcibly()` 强制终止
- 支持通过 `ProcessBuilderFactory` 函数式接口注入构造器，便于单元测试 mock

### 2.7 资源管理层

负责渲染器工作目录下 `Resources/` 资源的统一扫描与定位，模型与语音包目录已分离：

- **ModelScanner**（`core/ModelScanner.java`，工具类）：基于渲染器路径定位资源目录并提供扫描能力
  - `resolveResourcesDir(rendererPath)` → `<rendererDir>/Resources`
  - `resolveModelsDir(rendererPath)` → `Resources/Models`
  - `resolveVoicePacksDir(rendererPath)` → `Resources/VoicePacks`
  - `scanAvailableModels(rendererPath)`：列出 `Resources/Models` 下所有合法模型目录（判定条件：目录名 + 同名 `.model3.json` 存在），返回排序后的模型短名称列表，供 Dashboard 模型切换 ComboBox 使用
  - `getModelInfo(rendererPath, modelName)`：解析指定模型的 `.model3.json`，委托 `ModelInfoParser` 输出 `ModelInfo`
- **目录约定**：
  - Live2D 模型 → `Resources/Models/<ModelName>/<ModelName>.model3.json`
  - 语音包 → `Resources/VoicePacks/<VoicePack>/meta.mko`（由 `VoicePackScanner` 扫描）
- 资源目录分离使模型与语音包可独立增删，与 [2.4 语音包挂载层](#24-语音包挂载层phase-3a--已实现) 的解耦挂载设计相呼应

---

## 三、闲时行为设计

### 3.1 MVP 方案

MVP 阶段不实现宠物养成状态系统（心情/活力/好感度）。闲时行为采用简单随机策略：

```plain
定时任务触发（ScheduledExecutorService）
     │
     ▼
从模型可用的闲时动作池（idle_motions）中随机选择一个
     │
     ▼
下发 play_motion 指令到渲染器
```

### 3.2 扩展预留

架构预留权重打分决策机制，未来引入状态系统后：

```plain
定时任务触发
     │
     ▼
获取当前状态属性（心情、活力、好感度...）
     │
     ▼
为每个候选动作计算权重分
  - 心情高 + 活力高 → 欢快动作权重高
  - 心情低 + 活力低 → 打瞌睡/委屈动作权重高
     │
     ▼
加权随机选择动作
     │
     ▼
下发 play_motion 指令
```

状态数据支持持久化（关闭应用后下次启动可从上次状态继续），具体状态属性和衰减规则待未来版本设计。
