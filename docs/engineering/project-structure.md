# 项目目录结构

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、渲染引擎目录结构（MVP + Phase 1 已实现）

> **代码风格说明**：渲染引擎源码基于 Cubism SDK Samples 的 `LApp*` 命名风格开发，而非文档早期规划的模块化目录结构。这是因为 Cubism SDK 的 Samples/Common 提供了大量基础实现（内存分配器、模型加载、纹理管理、触摸管理等），直接复用 `LApp*` 代码并在其上扩展，比从零搭建模块化架构更高效。

```plain
desktop_pet/
│
├── renderer/                             # C++ 渲染引擎
│   ├── CMakeLists.txt                    # 渲染引擎 CMake（管理编译目标和依赖）
│   ├── src/
│   │   ├── main.cpp                      # 入口函数
│   │   ├── LAppDefine.cpp/.hpp           # 全局常量定义（窗口尺寸、WebSocket 端口、资源路径等）
│   │   ├── LAppDelegate.cpp/.hpp         # 引擎主类（单例，生命周期管理、主循环、窗口创建、事件分发）
│   │   ├── LAppLive2DManager.cpp/.hpp    # Live2D 模型管理器（模型加载/切换/释放、场景管理）
│   │   ├── LAppModel.cpp/.hpp            # 单个 Live2D 模型封装（继承 CubismUserModel，动作/表情/物理/渲染）
│   │   ├── LAppView.cpp/.hpp             # 视图层（坐标变换、触摸事件→模型坐标、渲染调度）
│   │   ├── LAppTextureManager.cpp/.hpp   # 纹理加载与缓存（stb_image 解码 PNG → OpenGL 纹理）
│   │   ├── LAppPal.cpp/.hpp              # 平台抽象层（文件读取、时间获取、日志输出）
│   │   └── network/                      # 网络模块（Phase 1 实现）
│   │       ├── Protocol.cpp/.hpp         # Envelope 协议序列化/反序列化（nlohmann/json）
│   │       ├── MessageHandler.cpp/.hpp   # 消息路由（按 action 分发 command，过滤 response）
│   │       ├── WebSocketClient.cpp/.hpp  # IXWebSocket 客户端封装（连接/断连/消息收发）
│   │       ├── CommandHandlers.cpp/.hpp  # 指令处理器注册（load_model、play_motion 等）
│   │       └── EventEmitter.cpp/.hpp     # 事件上报（hit、drag_start/end、model_loaded 等）
│   ├── third_party/                      # 渲染引擎额外第三方库
│   │   ├── nlohmann/                     # nlohmann/json 3.12.0（Header-only）
│   │   ├── ixwebsocket/                  # IXWebSocket 11.4.6（源码编译）
│   │   └── googletest/                   # Google Test 1.17.0（源码编译）
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

## 二、控制面板目录结构（Phase 2 已实现）

```plain
controller/                               # Java 控制面板
├── pom.xml                               # Maven 项目配置
└── src/
    ├── main/
    │   ├── java/
    │   │   ├── module-info.java           # Java 模块声明（JPMS）
    │   │   └── com/desktoppet/
    │   │       ├── App.java               # 应用入口（JavaFX Application，调用 AppOrchestrator）
    │   │       ├── ui/                    # UI 层（JavaFX）
    │   │       │   ├── MainWindowController  # 主窗口 FXML 控制器
    │   │       │   ├── SettingsPanelController # 设置面板 FXML 控制器
    │   │       │   └── TrayManager        # 系统托盘集成（java.awt.SystemTray）
    │   │       ├── core/                  # 业务逻辑层
    │   │       │   ├── AppOrchestrator    # 生命周期编排器（启动/关闭/崩溃恢复的中枢）
    │   │       │   ├── PetStateManager    # 宠物运行时状态管理（线程安全，ReentrantReadWriteLock）
    │   │       │   ├── InteractionHandler # 交互事件处理（hit→play_motion 指令生成）
    │   │       │   ├── Scheduler          # 定时闲时动作触发（ScheduledExecutorService）
    │   │       │   ├── ConfigManager      # 配置读写与持久化（~/.config/desktop-pet/config.json）
    │   │       │   ├── ModelInfoParser    # 解析 model3.json（动作组/表情/HitArea）
    │   │       │   └── audio/             # Phase 3 音频架构预留
    │   │       │       └── AudioMapping   # 音频映射记录
    │   │       ├── network/               # 网络层
    │   │       │   ├── PetWebSocketServer # Java-WebSocket 服务端（单连接管理、消息回调）
    │   │       │   ├── MessageDispatcher  # 按 type+action 路由消息（含 CompletableFuture 回执）
    │   │       │   └── Protocol           # Envelope 协议封装（Gson，response 字段在顶层）
    │   │       ├── model/                 # 数据模型（Java 21 Records）
    │   │       │   ├── Envelope           # WebSocket 消息信封
    │   │       │   ├── PetState           # 宠物运行时状态快照（不可变）
    │   │       │   ├── PetConfig          # 用户配置（含 WindowConfig、ModelSettingsConfig 等子记录）
    │   │       │   ├── ModelConfig         # 模型行为映射（HitAction 列表）
    │   │       │   ├── ModelInfo          # 模型元信息（动作组、表情、HitArea）
    │   │       │   ├── Motion             # 动作定义
    │   │       │   └── HitAction          # 点击区域→动作映射
    │   │       └── util/                  # 工具类
    │   │           └── ProcessManager     # 渲染器进程启动/监控/停止（ProcessBuilder）
    │   └── resources/
    │       ├── fxml/                      # JavaFX FXML 布局文件
    │       │   ├── main-window.fxml       # 主窗口布局
    │       │   └── settings-panel.fxml    # 设置面板布局
    │       └── logback.xml                # Logback 日志配置（控制台 + 文件轮转）
    └── test/
        ├── java/com/desktoppet/
        │   ├── core/                      # 业务逻辑测试
        │   │   ├── ConfigManagerTest      # 配置管理测试（7 cases）
        │   │   ├── PetStateManagerTest    # 状态管理测试（7 cases，含并发）
        │   │   ├── InteractionHandlerTest # 交互处理测试（5 cases）
        │   │   ├── SchedulerTest          # 定时任务测试（6 cases）
        │   │   └── ModelInfoParserTest    # 模型解析测试（6 cases）
        │   ├── network/                   # 网络层测试
        │   │   ├── ProtocolTest           # 协议序列化测试（12 cases，含 C++ 互操作）
        │   │   ├── MessageDispatcherTest  # 消息分发测试（6 cases）
        │   │   └── PetWebSocketServerTest # WebSocket 集成测试（6 cases，真实端口）
        │   ├── ui/                        # UI 测试（TestFX + Monocle 无头模式）
        │   │   ├── MainWindowTest         # 主窗口测试（4 cases）
        │   │   └── SettingsPanelTest      # 设置面板测试（4 cases）
        │   └── util/
        │       └── ProcessManagerTest     # 进程管理测试（5 cases）
        └── resources/
            └── logback-test.xml           # 测试专用日志配置（仅控制台，DEBUG 级别）
```

---

## 三、完整目录结构总览（Phase 3 计划扩展）

Phase 3（音频模块）引入后，在现有结构基础上扩展：

```plain
desktop_pet/                              # 项目根目录
├── renderer/                             # C++ 渲染引擎（见第一节）
│   └── src/
│       ├── ...                           # 现有 LApp* 代码
│       ├── network/                      # 网络模块（已实现）
│       └── audio/                        # 音频模块（Phase 3 计划）
│           ├── AudioManager              # 音频资源管理与缓存（从独立目录加载）
│           ├── AudioPlayer               # OpenAL 播放器封装
│           ├── AudioSync                 # 动作-音频时间戳对齐
│           └── AudioMappingCache         # 接收并缓存控制面板下发的音频映射
│
├── controller/                           # Java 控制面板（见第二节）
│
├── third_party/                          # 项目级第三方依赖
│   └── CubismSdkForNative/              # Cubism Native SDK
│
├── shared/                               # 共享资源（Phase 3 计划）
│   ├── models/                           # Live2D 模型文件（不含音频）
│   ├── audio/                            # 音频文件独立目录（与模型分离存储）
│   └── assets/                           # 其他共享资源
│
└── docs/                                 # 项目文档
```
