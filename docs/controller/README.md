# 控制面板设计 (Java)

> **实现状态**：Phase 2（控制面板）已完成，Phase 3a（语音包挂载 Java 侧）已完成。控制面板包括 Tab 式 UI、多实例管理、业务逻辑、进程管理、系统托盘、崩溃恢复、语音包扫描/解析/挂载等核心功能。
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

- **主窗口（MainWindowController + `main-window.fxml`）**：Tab 容器，管理 Dashboard/Settings/Actions/Advanced 四个 Tab 页的加载与切换。`App.java` 继承 `javafx.application.Application`，作为 JavaFX 生命周期入口，在 `start()` 中创建 `AppOrchestrator`
- **Dashboard Tab（DashboardTabController + `tab-dashboard.fxml`）**：宠物当前状态显示（连接状态、当前模型、动作组信息）、模型切换（ComboBox 选择可用模型）、渲染器重启按钮、运行时日志（ListView）
- **Settings Tab（SettingsTabController + `tab-settings.fxml`）**：行为配置——拖拽模式切换（ToggleGroup: direct/physics）、闲时动作间隔（Spinner）、帧率模式（自适应/固定，Spinner 15-120fps）、窗口透明度（Slider）、开机自启（CheckBox）、HitArea 映射表（TableView）、语音包选择（ComboBox）
- **Actions Tab（ActionsTabController + `tab-actions.fxml`）**：动作/表情手动触发——动作组/索引/优先级选择（ComboBox），快捷动作按钮（FlowPane），表情切换（FlowPane）
- **Advanced Tab（AdvancedTabController + `tab-advanced.fxml`）**：高级设置
- **设置面板（SettingsPanelController + `settings-panel.fxml`）**：独立 Stage 的设置窗口（兼容旧入口）
- **系统托盘（TrayManager）**：使用 `java.awt.SystemTray` API（JavaFX 未提供原生托盘支持）。关闭窗口时隐藏到托盘而非退出（`Platform.setImplicitExit(false)`），双击托盘图标切换窗口可见性，右键菜单包含 显示/隐藏、设置、退出
- **宠物管理**：模型切换（ComboBox 选择模型目录）、模型导入
- **语音包管理（Phase 3a ✅）**：Settings Tab 中提供语音包选择 ComboBox，挂载/卸载操作通过 `AppOrchestrator` 协调
- **音频管理（Phase 3b）**：音频文件导入/删除、音频映射配置、音量控制。待后续实现

**UI 架构要点**：

| 关注点 | 方案 |
|:---|:---|
| 布局定义 | FXML 文件（`src/main/resources/fxml/`），每个 Tab 独立 FXML，通过 `FXMLLoader` 加载 |
| 样式管理 | CSS 样式表（`src/main/resources/css/`），支持运行时主题切换 |
| 事件绑定 | FXML Controller 类，使用 `@FXML` 注解绑定 UI 组件 |
| 线程安全 | UI 更新必须通过 `Platform.runLater()` 回到 JavaFX Application Thread；AWT 操作（托盘）通过 `SwingUtilities.invokeLater()` |
| 可视化设计 | 推荐使用 JavaFX Scene Builder 辅助设计 FXML 布局 |
| 模块系统 | `module-info.java` 声明 JPMS 模块，`opens` 包给 `javafx.fxml`、`com.google.gson` 和 `com.google.protobuf` |
| 多实例模型 | `PetInstance` 使用 JavaFX Properties（`StringProperty`、`IntegerProperty` 等），支持 UI 双向绑定 |

### 2.2 业务逻辑层

- **生命周期编排器（AppOrchestrator）**：**控制面板的核心中枢**，负责启动/关闭的完整编排：配置加载 → WebSocket Server 启动 → 事件处理器注册 → 渲染器进程启动 → 握手 → 模型加载 → 位置恢复。同时负责崩溃恢复（指数退避重启）和断连处理（关键指令缓存）。详见 [启动流程](../system/startup.md)
- **状态管理器（PetStateManager）**：维护宠物运行时状态。使用 `ReentrantReadWriteLock` 保证线程安全，`getState()` 返回不可变的 `PetState` 快照（Java Record）。当前不实现心情/活力等养成属性，架构预留权重打分扩展
- **交互处理器（InteractionHandler）**：接收渲染器上报的 `hit` 事件，查询模型行为映射配置，决定后续响应（构建 `play_motion` 指令并通过 `Protocol.createCommand()` 序列化发送）。内置默认映射（`head→TapHead`、`body→TapBody`），支持大小写容错（渲染器发送小写 key，配置使用 PascalCase）
- **定时任务（Scheduler）**：使用 `java.util.concurrent.ScheduledExecutorService` 实现定时闲时动作触发。支持 `pause()`/`resume()`（断连时暂停、重连后恢复）、`setIdleMotions()` 动态更新动作池、`updateInterval()` 修改触发间隔
- **配置管理器（ConfigManager）**：基于 Gson 读写 JSON 配置文件（`~/.config/desktop-pet/config.json`）。首次运行自动创建默认配置，JSON 损坏时降级为默认值（不崩溃），部分字段缺失时从 `PetConfig.defaults()` 合并。JSON 使用 `snake_case` 键名（`position_x`、`current_model_name`），Java Record 使用 `camelCase`
- **模型信息解析器（ModelInfoParser）**：直接解析 Cubism 的 `.model3.json` 文件，提取动作组（Map<String, Integer>）、表情列表、HitArea 列表。**重要**：渲染器的 `model_loaded` 事件中 motions/expressions 始终为空数组，Java 端必须自行解析模型文件获取这些信息
- **HitArea 缓存管理器（HitAreaCacheManager）**：缓存每个模型的 HitArea 列表到 `~/.config/desktop-pet/hit_area_cache.json`，避免重复解析模型文件
- **音频映射管理器（AudioMappingManager）**（Phase 3b 架构预留）：已定义 `AudioMapping` 记录（`motionGroup` + `audioPath`），完整实现待 Phase 3b

### 2.3 多实例管理层

支持同时运行多个宠物实例，每个实例独立配置、独立渲染器进程：

- **PetInstance**：宠物实例的 JavaFX 可观察模型。使用 JavaFX Properties（`StringProperty`、`BooleanProperty` 等）实现 UI 双向绑定。包含 label、model、status、connected、opacity、dragMode、idleInterval、窗口位置/尺寸、voicePack、volume 等属性。内置日志缓冲（`ObservableList<String>`，最多 50 条）。提供 `fromInstanceConfig()` / `toInstanceConfig()` 与持久化配置的双向转换
- **InstanceConfig**（Java Record）：单个实例的持久化配置——id（UUID）、label、rendererPath、modelName、modelScale、窗口位置/尺寸/透明度、dragMode、idleInterval、targetFps、autoStart、currentExpression、voicePack、volume
- **InstanceConfigManager**：每个实例配置存储为独立 JSON 文件（`~/.config/desktop-pet/instances/{uuid}.json`），支持 save/load/delete/loadAll
- **PanelConfig**（Java Record）：面板级配置——窗口位置/尺寸、主题、实例 ID 列表（`List<String> instanceIds`）
- **PanelState**（Java Record）：面板状态快照，包含实例状态列表
- **PanelStateManager**：管理 `~/.config/desktop-pet/panel.json`，支持从旧版 `panel-state.json` 自动迁移（创建迁移后的实例配置文件并备份旧文件）

### 2.4 语音包挂载层（Phase 3a ✅ 已实现）

实现语音包与模型的解耦挂载，Java 侧核心组件已完成：

- **VoicePackScanner**：扫描 Resources 目录，识别含 `meta.mko` 的语音包目录。返回排序后的语音包名列表
- **MetaMkoParser**：使用 protobuf-java 解析 `meta.mko` 二进制文件（`Bundle.parseFrom()`），输出 `VoicePackInfo`。提取元数据（displayName、code）、动作分组（groups）、行为模块（modules）、每个 action 的 motion/audio/lipSync/doc/fadeIn/fadeOut
- **VoicePackInfo**（Java Record）：语音包元数据模型——dirName、displayName、code、basePath、groups（`Map<String, VoicePackGroup>`）、modules（`List<VoicePackModule>`）
- **VoicePackGroup**（Java Record）：动作分组——code（事件名）、name（显示名）、priority、actions 列表
- **VoicePackAction**（Java Record）：单条动作映射——id、motionPath、audioPath、lipSyncPath、doc、fadeInMs、fadeOutMs
- **VoicePackModule**（Java Record）：行为模块——key、priority、filePath
- **MountConfig**（Java Record）：挂载配置——modelName、voicePackName（null 表示未挂载）
- **MountConfigManager**：挂载关系持久化到 `~/.config/desktop-pet/mount.json`。支持 `loadForModel()`、`saveForModel()`、`loadAll()`。JSON 损坏时降级为空配置
- **MountedBehaviorEngine**：运行时行为引擎。`hasGroupForArea(areaId)` 检查语音包是否有对应事件组，`buildMotionCommand(areaId)` 从事件组随机选择一条 action 并构建 `play_motion_ext` 指令（绝对路径 + priority + fadeIn/fadeOut），同时附带 audio_path（如有）

> **详细设计**参见 [外置语音包挂载](../system/voice-pack-mounting.md)。

### 2.5 通信层（Network Layer）

基于 Java-WebSocket 库实现 WebSocket 服务端（控制面板为常驻进程，承担 Server 角色）：

> **注意**：以下为 Phase 1/Phase 2 实现，描述未变化。

- **PetWebSocketServer**：封装服务端启动、单连接管理（`activeConnection` 跟踪，新连接自动替换旧连接）、消息收发。使用独立线程处理网络 I/O，通过 `setMessageCallback` / `setConnectionCallback` 回调转发消息和连接状态变化
- **MessageDispatcher**：按消息 `type` 分路由——`response` 类型通过 `CompletableFuture` 匹配 `id` 完成回执（`expectResponse()` + `orTimeout()`），`event` 类型按 `action` 路由到注册的 `Consumer<Envelope>` 处理器。未注册 action 记录 WARN 日志，处理器异常隔离不传播
- **Protocol**：封装 Envelope 格式的序列化/反序列化（Gson）。**关键约定**：response 的 `success`/`error_code`/`error_message` 字段位于 JSON **顶层**（与 C++ 端保持一致），不在 `payload` 内。提供 `createCommand()`/`createEvent()`/`createResponse()` 工厂方法和 UUID 生成
- **连接管理**：监测渲染引擎连接状态，断开时通知 `AppOrchestrator`。详见 [容错与错误处理](../system/fault-tolerance.md)

### 2.6 进程管理器（ProcessManager）

负责渲染器进程的生命周期管理：

- 使用 `ProcessBuilder` 启动渲染器可执行文件，工作目录设为渲染器二进制所在目录（确保 `Resources/` 路径正确解析）
- 通过 `Process.onExit()` 监听进程退出，通知 `AppOrchestrator`。非零退出码视为崩溃，触发自动重启流程
- 停止流程：先通过回调发送 `shutdown` 指令 → 轮询等待进程退出（100ms 间隔，最长 5 秒）→ 超时后 `Process.destroyForcibly()` 强制终止
- 支持通过 `ProcessBuilderFactory` 函数式接口注入构造器，便于单元测试 mock

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
