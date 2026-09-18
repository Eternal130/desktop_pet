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
    ├── controller_qt/                    # Qt 控制面板设计与测试文档
    ├── protocol/                         # 通信协议
    ├── interaction/                      # 交互设计
    ├── system/                           # 系统设计
    └── engineering/                      # 工程化
```

---

## 二、控制面板目录结构（controller_qt，Phase 5-9 已实现）

```plain
controller_qt/                           # Qt 6 控制面板（C++17 / QML）
├── CMakeLists.txt                       # Qt6 find_package、qt_add_executable、qt_add_qml_module
├── README.md                            # 完整文档（特性/构建/打包/测试/架构）
├── src/
│   ├── main.cpp                         # 入口（QGuiApplication + QQmlApplicationEngine，
│   │                                    #   上下文属性：instanceManager、trayManager、autoLaunch、
│   │                                    #   panelConfig、environmentChecker、windowStateSaver）
│   ├── poc_main.cpp                     # Phase-0 PoC：驱动渲染器完整 WS 往返
│   ├── network/                         # 网络层
│   │   ├── Protocol                     # 20 个类型化命令工厂 + Envelope 辅助函数
│   │   ├── WsServer                     # QWebSocketServer（127.0.0.1:9001），三重门令牌握手，
│   │   │                                #   按 instanceId 多实例路由，重连时替换连接
│   │   ├── MessageDispatcher            # 按 type 路由 Envelope（response/event/command）
│   │   ├── PendingRequests              # 按命令 id 的 10 秒超时回执表
│   │   ├── EventRegistry                # 14 类事件默认处理（日志）+ 按实例注册
│   │   └── ThreadMarshal                # WS I/O 线程 → GUI 线程编组
│   ├── core/                            # 业务逻辑层
│   │   ├── InstanceSession (+5 个拆分 TU) # 每宠物编排器：拥有 ProcessManager、
│   │   │                                #   MessageDispatcher、EventRegistry、Scheduler、
│   │   │                                #   InteractionHandler、RestartController、MonitorDataModel
│   │   ├── InstanceManager              # QAbstractListModel 侧边栏花名册，按 instanceId 分流
│   │   ├── Scheduler                    # QTimer 闲时动作节奏
│   │   ├── InteractionHandler           # hit → play_motion（3 级大小写容错查找）
│   │   ├── HitAreaCacheManager          # modelName → hitAreas 的 JSON 缓存
│   │   ├── RestartController            # 崩溃恢复（指数退避，最大 5 次）
│   │   ├── MountedBehaviorEngine        # 语音包行为引擎（挂载时优先于 InteractionHandler）
│   │   ├── VoicePackScanner             # 扫描识别语音包（含 meta.mko 的目录）
│   │   ├── MetaMkoParser                # 解析 meta.mko（手写 protobuf wire-format reader）
│   │   ├── ModelInfoParser              # 解析 model3.json（动作组/表情/HitArea）
│   │   ├── ModelScanner                 # 扫描 Resources 目录识别可用模型
│   │   ├── PanelConfigController        # PanelConfig 4 个行为字段的 QML 桥
│   │   ├── ProcessManager               # 渲染器子进程生命周期（QProcess）
│   │   ├── StartupSalvo                 # ready 后的 7 条启动指令序列
│   │   ├── InstanceConfig(+Manager)     # 实例配置（instances/{uuid}.json，QSaveFile 原子写入）
│   │   ├── PanelConfig(+StateManager)   # 面板配置（panel.json）
│   │   ├── ConfigDir / PathResolve      # 配置目录与路径解析（~/.config/desktop-pet/）
│   │   ├── EnvironmentChecker           # 环境自检（欢迎页摘要）
│   │   └── WindowStateSaver             # 面板窗口状态保存
│   ├── system/                          # 系统集成层
│   │   ├── AutoLaunchManager            # 开机自启（Win reg.exe via QProcess / Linux .desktop）
│   │   ├── TrayManager                  # QSystemTrayIcon 封装（QML 弹出菜单）
│   │   └── ResourceStatsCollector       # Win GetProcessTimes/GetProcessMemoryInfo、Linux /proc/self/*
│   ├── ui/                              # QML 桥接层
│   │   ├── MonitorDataModel             # 60 采样环形缓冲（COW）+ mergeController/mergeRenderer
│   │   ├── VoicePackController          # 语音包页 QML 桥（发现 + 元数据 + 挂载矩阵）
│   │   └── NotificationStreamController # 通知流 QML 桥（气泡推送/消失/testBubble）
│   ├── logging/                         # Logging.cpp（spdlog 轮转文件 + Qt 消息桥接）
│   └── protobuf/                        # bundles.proto（schema 参考 — 不编译）
├── qml/
│   ├── Main.qml                         # FluWindow + FluAppBar + FluNavigationView 壳，5 页路由
│   ├── Theme.qml                        # 主题单例
│   ├── components/                      # StatusPill、SettingRow、SectionCard、Chip
│   └── pages/                           # WelcomePage、InstanceDetailPage、MonitorPage、
│                                        #   VoicePackPage、SettingsPage
└── tests/                               # 41 个 QTest 二进制（每个自带 windeployqt 后构建步骤）
```

---
## 三、完整目录结构总览（Phase 3b 渲染器侧已完成，控制器侧待实现）

Phase 3a（语音包挂载）控制面板侧（controller_qt）与渲染器侧（`play_motion_ext`）均已完成。Phase 2.x（Vulkan 后端）已完成。Phase 3b 渲染器侧音频播放（`AudioManager`，miniaudio + libvorbis）已实现，控制器侧音频映射管理 UI 待实现。

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
├── controller_qt/                        # Qt 6 控制面板（见第二节，Phase 5-9 已实现）
│
├── third_party/                          # 项目级第三方依赖
│   └── CubismSdkForNative/              # Cubism Native SDK
│
└── docs/                                 # 项目文档
```
