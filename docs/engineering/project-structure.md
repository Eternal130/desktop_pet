# 项目目录结构

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、渲染引擎目录结构（MVP + Phase 1 + Phase 2.x + Phase 3b 已实现）

> **代码风格说明**：渲染引擎源码基于 Cubism SDK Samples 的 `LApp*` 命名风格开发，而非文档早期规划的模块化目录结构。这是因为 Cubism SDK 的 Samples/Common 提供了大量基础实现（内存分配器、模型加载、纹理管理、触摸管理等），直接复用 `LApp*` 代码并在其上扩展，比从零搭建模块化架构更高效。

```plain
desktop_pet/
│
├── renderer/                             # C++ 渲染引擎
│   ├── CMakeLists.txt                    # 渲染引擎 CMake（管理编译目标和依赖，含 USE_VULKAN 开关）
│   ├── src/
│   │   ├── main.cpp                      # 入口函数
│   │   ├── LAppDefine.cpp/.hpp           # 全局常量定义（窗口尺寸、WebSocket 端口、资源路径等）
│   │   ├── LAppDelegate.cpp/.hpp         # 引擎主类（单例，生命周期管理、主循环、窗口创建、事件分发）
│   │   ├── LAppLive2DManager.cpp/.hpp    # Live2D 模型管理器（模型加载/切换/释放、场景管理）
│   │   ├── LAppModel.cpp/.hpp            # 单个 Live2D 模型封装（继承 CubismUserModel，动作/表情/物理/渲染）
│   │   ├── LAppView.cpp/.hpp             # 视图层（坐标变换、触摸事件→模型坐标、渲染调度）
│   │   ├── LAppTextureManager.cpp/.hpp   # 纹理加载与缓存（stb_image 解码 PNG → 纹理）
│   │   ├── LAppPal.cpp/.hpp              # 平台抽象层（文件读取、时间获取、日志输出）
│   │   ├── AudioManager.cpp/.hpp         # 音频播放管理（Phase 3b，miniaudio + libvorbis 播放 OGG）
│   │   ├── graphics/                     # 渲染后端层（Phase 2.x，OpenGL/Vulkan 双后端解耦）
│   │   │   ├── IGraphicsBackend.hpp      # 渲染后端抽象接口（6 方法，GL/Vulkan 共享）
│   │   │   ├── OpenGLBackend.cpp/.hpp    # OpenGL 实现（GLEW，MVP 已有）
│   │   │   └── VulkanBackend.cpp/.hpp    # Vulkan 实现（979 行，Instance→Device→Swapchain→Render→Present，dynamic rendering + vkCmdCopyImageToBuffer）
│   │   ├── platform/                     # 平台/窗口层（GL/Vulkan 共享）
│   │   │   └── WindowManager.cpp/.hpp    # 窗口与上下文管理（GLFW，被双后端共享）
│   │   └── network/                      # 网络模块（Phase 1 实现）
│   │       ├── Protocol.cpp/.hpp         # Envelope 协议序列化/反序列化（nlohmann/json）
│   │       ├── MessageHandler.cpp/.hpp   # 消息路由（按 action 分发 command，过滤 response）
│   │       ├── WebSocketClient.cpp/.hpp  # IXWebSocket 客户端封装（连接/断连/消息收发）
│   │       ├── CommandHandlers.cpp/.hpp  # 指令处理器注册（load_model、play_motion、play_motion_ext、play_audio、stop_audio、set_volume 等）
│   │       └── EventEmitter.cpp/.hpp     # 事件上报（hit、drag_start/end、model_loaded 等）
│   ├── third_party/                      # 渲染引擎额外第三方库
│   │   ├── nlohmann/                     # nlohmann/json 3.12.0（Header-only）
│   │   ├── ixwebsocket/                  # IXWebSocket 11.4.6（源码编译）
│   │   ├── googletest/                   # Google Test 1.17.0（源码编译）
│   │   └── miniaudio.h                   # miniaudio single-header（Phase 3b 音频，vendored）
│   └── tests/                            # C++ 单元测试
│       ├── placeholder_test.cpp          # 占位测试
│       ├── ProtocolTest.cpp              # Protocol 序列化/反序列化测试（11 cases）
│       └── MessageHandlerTest.cpp        # MessageHandler 路由测试（6 cases）
│
├── third_party/                          # 项目级第三方依赖
│   └── CubismSdkForNative/              # Cubism Native SDK（Core + Framework + Samples）
│       ├── Core/                         # 闭源预编译库（C 接口）
│       ├── Framework/                    # 开源 C++ 框架（编译为静态库）
│       └── Samples/                      # 示例代码 + 内置第三方库 + 示例模型
│           ├── Common/                   # LApp* 公共代码（本项目复用）
│           ├── OpenGL/thirdParty/        # GLFW、GLEW、stb（本项目使用）
│           └── Resources/               # 8 个免费示例模型
│
└── docs/                                 # 项目文档（层次化结构）
    ├── README.md                         # 架构总览 + 文档索引
    ├── renderer/                         # 渲染引擎设计
    ├── controller/                       # 控制面板设计
    ├── protocol/                         # 通信协议
    ├── interaction/                      # 交互设计
    ├── system/                           # 系统设计
    └── engineering/                      # 工程化
```

---

## 二、控制面板目录结构（Phase 2 + Phase 3a 已实现）

```plain
controller/                               # Java 控制面板
├── pom.xml                               # Maven 项目配置
└── src/
    ├── main/
    │   ├── java/
    │   │   ├── module-info.java           # Java 模块声明（JPMS）
    │   │   └── com/desktoppet/
    │   │       ├── App.java               # 应用入口（JavaFX Application，调用 AppOrchestrator）
    │   │       ├── Launcher.java          # 非模块化启动入口（绕过 JPMS 限制）
    │   │       ├── ui/                    # UI 层（JavaFX，Tab 式布局）
    │   │       │   ├── MainWindowController    # 主窗口 Tab 容器（main-window.fxml）
    │   │       │   ├── DashboardTabController  # Dashboard Tab（状态/模型切换/日志）
    │   │       │   ├── SettingsTabController   # Settings Tab（行为配置/语音包选择）
    │   │       │   ├── ActionsTabController    # Actions Tab（动作/表情手动触发）
    │   │       │   ├── AdvancedTabController   # Advanced Tab（高级设置）
    │   │       │   ├── SettingsPanelController # 设置面板（独立 Stage，兼容旧入口）
    │   │       │   └── TrayManager             # 系统托盘集成（java.awt.SystemTray）
    │   │       ├── core/                  # 业务逻辑层
    │   │       │   ├── AppOrchestrator    # 生命周期编排器（启动/关闭/崩溃恢复的中枢）
    │   │       │   ├── PetStateManager    # 宠物运行时状态管理（线程安全，ReentrantReadWriteLock）
    │   │       │   ├── InteractionHandler # 交互事件处理（hit→play_motion 指令生成）
    │   │       │   ├── Scheduler          # 定时闲时动作触发（ScheduledExecutorService）
    │   │       │   ├── ConfigManager      # 全局配置读写（~/.config/desktop-pet/config.json）
    │   │       │   ├── InstanceConfigManager   # 实例配置管理（instances/{uuid}.json）
    │   │       │   ├── PanelStateManager       # 面板配置管理（panel.json，含旧版迁移）
    │   │       │   ├── ModelInfoParser    # 解析 model3.json（动作组/表情/HitArea）
    │   │       │   ├── ModelScanner       # 扫描 Resources 目录识别可用模型
    │   │       │   ├── HitAreaCacheManager     # HitArea 缓存（hit_area_cache.json）
    │   │       │   ├── VoicePackScanner   # 扫描识别语音包（含 meta.mko 的目录）
    │   │       │   ├── MetaMkoParser      # 解析 meta.mko（Protobuf → VoicePackInfo）
    │   │       │   ├── MountConfigManager # 挂载配置持久化（mount.json）
    │   │       │   ├── MountedBehaviorEngine   # 语音包运行时行为引擎（事件→motion 指令）
    │   │       │   └── audio/             # Phase 3b 音频架构预留
    │   │       │       └── AudioMapping   # 音频映射记录
    │   │       ├── network/               # 网络层
    │   │       │   ├── PetWebSocketServer # Java-WebSocket 服务端（单连接管理、消息回调）
    │   │       │   ├── MessageDispatcher  # 按 type+action 路由消息（含 CompletableFuture 回执）
    │   │       │   └── Protocol           # Envelope 协议封装（Gson，response 字段在顶层）
    │   │       ├── model/                 # 数据模型（Java 21 Records）
    │   │       │   ├── Envelope           # WebSocket 消息信封
    │   │       │   ├── PetState           # 宠物运行时状态快照（不可变）
    │   │       │   ├── PetConfig          # 用户配置（含 WindowConfig、ModelSettingsConfig 等子记录）
    │   │       │   ├── WindowConfig       # 窗口配置（positionX/Y、width/height、opacity）
    │   │       │   ├── BehaviorConfig     # 行为配置（dragMode、idleIntervalSeconds、targetFps）
    │   │       │   ├── SystemConfig       # 系统配置（autoStart）
    │   │       │   ├── ModelSettingsConfig # 模型设置配置
    │   │       │   ├── ModelConfig        # 模型行为映射（HitAction 列表）
    │   │       │   ├── ModelInfo          # 模型元信息（动作组、表情、HitArea）
    │   │       │   ├── Motion             # 动作定义
    │   │       │   ├── HitAction          # 点击区域→动作映射
    │   │       │   ├── PetInstance        # 宠物实例 UI 模型（JavaFX Properties，支持数据绑定）
    │   │       │   ├── InstanceConfig     # 实例持久化配置（Record）
    │   │       │   ├── InstanceState      # 实例状态快照（Record）
    │   │       │   ├── PanelConfig        # 面板配置（窗口位置/主题/实例ID列表）
    │   │       │   ├── PanelState         # 面板状态快照
    │   │       │   ├── MountConfig        # 语音包挂载配置（modelName + voicePackName）
    │   │       │   ├── VoicePackInfo      # 语音包元数据（dirName/displayName/groups/modules）
    │   │       │   ├── VoicePackGroup     # 语音包动作分组（code/name/priority/actions）
    │   │       │   ├── VoicePackAction    # 语音包单条动作（motion/audio/lipSync/doc/fade）
    │   │       │   └── VoicePackModule    # 语音包行为模块（key/priority/filePath）
    │   │       └── util/                  # 工具类
    │   │           ├── ProcessManager     # 渲染器进程启动/监控/停止（ProcessBuilder）
    │   │           └── AutoLaunchManager  # 开机自启管理（OS 特定：Windows 注册表 / Linux .desktop）
    │   └── resources/
    │       ├── fxml/                      # JavaFX FXML 布局文件
    │       │   ├── main-window.fxml       # 主窗口（Tab 容器）
    │       │   ├── tab-dashboard.fxml     # Dashboard Tab
    │       │   ├── tab-settings.fxml      # Settings Tab
    │       │   ├── tab-actions.fxml       # Actions Tab
    │       │   ├── tab-advanced.fxml      # Advanced Tab
    │       │   └── settings-panel.fxml    # 设置面板（独立 Stage）
    │       └── logback.xml                # Logback 日志配置（控制台 + 文件轮转）
    └── test/
        ├── java/com/desktoppet/
        │   ├── core/                      # 业务逻辑测试
        │   │   ├── ConfigManagerTest      # 配置管理测试（7 cases）
        │   │   ├── PetStateManagerTest    # 状态管理测试（7 cases，含并发）
        │   │   ├── PanelStateManagerTest  # 面板状态管理测试
        │   │   ├── InteractionHandlerTest # 交互处理测试（5 cases）
        │   │   ├── SchedulerTest          # 定时任务测试（6 cases）
        │   │   ├── ModelInfoParserTest    # 模型解析测试（6 cases）
        │   │   ├── ModelScannerTest       # 模型扫描测试
        │   │   ├── VoicePackScannerTest   # 语音包扫描测试
        │   │   ├── MetaMkoParserTest      # meta.mko 解析测试
        │   │   ├── MountConfigManagerTest # 挂载配置测试
        │   │   └── MountedBehaviorEngineTest # 行为引擎测试
        │   ├── network/                   # 网络层测试
        │   │   ├── ProtocolTest           # 协议序列化测试（12 cases，含 C++ 互操作）
        │   │   ├── MessageDispatcherTest  # 消息分发测试（6 cases）
        │   │   └── PetWebSocketServerTest # WebSocket 集成测试（6 cases，真实端口）
        │   ├── ui/                        # UI 测试（TestFX + Monocle 无头模式）
        │   │   ├── MainWindowTest         # 主窗口测试（4 cases）
        │   │   ├── SettingsPanelTest      # 设置面板测试（4 cases）
        │   │   └── SettingsTabTest        # Settings Tab 测试
        │   ├── integration/
        │   │   └── E2ESmokeTest           # 端到端冒烟测试
        │   └── util/
        │       └── ProcessManagerTest     # 进程管理测试（5 cases）
        └── resources/
            └── logback-test.xml           # 测试专用日志配置（仅控制台，DEBUG 级别）
```

---

## 三、完整目录结构总览（Phase 3b 渲染器侧已完成，控制器侧待实现）

Phase 3a（语音包挂载）Java 侧与渲染器侧（`play_motion_ext`）均已完成。Phase 2.x（Vulkan 后端）已完成。Phase 3b 渲染器侧音频播放（`AudioManager`，miniaudio + libvorbis）已实现，控制器侧（`AudioMappingManager`/UI）待实现。

```plain
desktop_pet/                              # 项目根目录
├── renderer/                             # C++ 渲染引擎（见第一节）
│   └── src/
│       ├── ...                           # 现有 LApp* 代码
│       ├── graphics/                     # 渲染后端层（Phase 2.x 已实现，IGraphicsBackend + OpenGLBackend + VulkanBackend）
│       ├── platform/                     # 窗口层（WindowManager，GL/Vulkan 共享）
│       ├── network/                      # 网络模块（已实现）
│       ├── AudioManager.cpp/.hpp         # 音频播放（Phase 3b 渲染器侧已实现，miniaudio + libvorbis OGG 播放）
│       └── audio/                        # 口型同步/文案气泡扩展（Phase 3c 计划）
│           ├── AudioSync                 # 动作-音频时间戳对齐（Phase 3c 计划）
│           └── AudioMappingCache         # 接收并缓存控制面板下发的音频映射（Phase 3b 控制器侧联动，待实现）
│
├── controller/                           # Java 控制面板（见第二节，Phase 2 + 3a 已实现，3b 控制器侧待实现）
│
├── third_party/                          # 项目级第三方依赖
│   └── CubismSdkForNative/              # Cubism Native SDK
│
└── docs/                                 # 项目文档
```
