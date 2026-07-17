# 架构总览

> 本文档描述桌面宠物项目的整体架构、技术栈、设计原则和职责边界。
> 详细设计请参阅各组件文档。

---

## 一、项目阶段与当前进度

本项目分为 4 个阶段，**MVP、Phase 1、Phase 2、Phase 2.x（Vulkan 渲染后端）已完成，Phase 3a 已完成，Phase 3b 渲染器侧已完成**。此外 **controller_qt（Qt 6 控制面板，Phase 5-9）已完成**：

| 功能 | MVP | Phase 1 | Phase 2 | Phase 2.x | Phase 3 |
|:---|:---:|:---:|:---:|:---:|:---:|
| Live2D 模型加载与渲染 | ✅ 已完成 | — | ✅ 控制面板动态切换 | ✅ Vulkan 后端 | — |
| 动画播放（动作/表情/眨眼/呼吸/物理演算） | ✅ 已完成 | — | — | — | — |
| 透明无边框置顶窗口 | ✅ 已完成 | — | — | — | — |
| 点击检测（HitArea → 即时动画反馈） | ✅ 已完成 | — | ✅ 事件上报到控制面板 | — | — |
| 窗口拖拽移动 | ✅ 已完成 | — | ✅ 位置上报与持久化 | — | — |
| 自适应帧率 | ✅ 已完成 | — | ✅ 可选固定帧率 | — | — |
| WebSocket 通信 | ✗ | ✅ 已完成（端口 9001） | — | — | — |
| Java 控制面板 | ✗ | — | ✅ 已完成（Tab 式 UI + 多实例） | — | — |
| 多实例管理 | ✗ | — | ✅ 已完成 | — | — |
| 外置语音包挂载 | ✗ | — | — | — | ✅ Phase 3a 已完成（Java 侧扫描/解析/挂载/行为引擎 + 渲染器 `play_motion_ext`） |
| 音频播放 | ✗ | — | — | — | ⚠️ 渲染器侧 ✅（`AudioManager`，miniaudio + libvorbis），控制器侧待实现 |
| 口型同步 + 文案气泡 | ✗ | — | — | — | ❌ 待开发（Phase 3c） |
| 闲时随机动作 | ✅ 已完成（渲染器内置） | — | ✅ 由控制面板 Scheduler 调度 | — | — |
| **controller_qt（Qt 6 控制面板）** | ✗ | — | — | — | — |

> **controller_qt Phase 5-9: ✅ 已完成** — Qt 6.10 / C++17 / QML 控制面板，JavaFX `controller/` 的 C++ 后继版本。多实例宠物管理（侧边栏 `QAbstractListModel`）、完整协议覆盖（25 命令 + 14 事件）、每实例配置持久化（`~/.config/desktop-pet/instances/{uuid}.json`，QSaveFile 原子写入）、闲时动作调度器、点击→动作处理器（3 级大小写容错查找）、崩溃恢复（指数退避，最大 5 次）、系统托盘（QSystemTrayIcon）、开机自启（Windows 注册表 / Linux `.desktop`）、资源监视器（QtCharts 火花线：CPU% + RSS）、语音包发现与挂载（手写 protobuf reader，无 libprotobuf 依赖）、字幕系统（16 个命名样式字段）、布局同步。**38 个 QTest 二进制全部通过**。蓝图 §9.5「永不崩溃」哲学合规性已审计（T25）。详见 `controller_qt/README.md`。

> **阶段说明**：Phase 1 = WebSocket 通信层，Phase 2 = Java 控制面板，Phase 2.x = Vulkan 渲染后端解耦（OpenGL/Vulkan 双后端，编译时切换），Phase 3 = 音频与语音包模块（细分为 3a 基础挂载、3b 音频播放、3c 口型同步、3d 行为图引擎）。
>
> **当前状态**：MVP、Phase 1、Phase 2、Phase 2.x（Vulkan 渲染后端）已完成，平台支持已扩展至 Windows（MinGW Makefiles，win32 环境）。Phase 2 控制面板已重构为 Tab 式 UI（Dashboard/Settings/Actions/Advanced），支持多宠物实例管理。Phase 2.x 在渲染引擎中引入 `IGraphicsBackend` 抽象接口，提供 `OpenGLBackend` 与 `VulkanBackend` 双实现，通过编译时开关 `-DUSE_VULKAN=ON` 切换（无运行时切换），`platform/WindowManager.cpp` 被 GL/Vulkan 共享。Phase 3a（语音包挂载）Java 侧与渲染器侧均已实现——Java 侧语音包扫描（`VoicePackScanner`）、meta.mko 解析（`MetaMkoParser`）、挂载配置持久化（`MountConfigManager`）、运行时行为引擎（`MountedBehaviorEngine`），渲染器侧 `play_motion_ext` 指令已注册。Phase 3b 渲染器侧音频播放已实现（`AudioManager`，miniaudio + libvorbis 播放 OGG，`play_audio`/`stop_audio`/`set_volume` 三指令已接入），控制器侧 `AudioMapping` record 仅定义、`AudioMappingManager` 与 UI 待实现。Phase 3c/3d 待后续实现。
>
> **controller_qt（Qt 6 控制面板，Phase 5-9）已全部完成**：Qt 6.10 / C++17 / QML，多实例宠物管理，完整协议覆盖（25 命令 + 14 事件），每实例配置持久化（QSaveFile 原子写入），闲时调度器，点击→动作处理，崩溃恢复（指数退避），系统托盘，开机自启，资源监视器（QtCharts），语音包发现与挂载（手写 protobuf reader），字幕系统，布局同步。38 个 QTest 二进制全部通过。蓝图 §9.5「永不崩溃」合规性已审计。详见 `controller_qt/README.md`。

---

## 二、整体架构

### 2.1 MVP 架构（渲染引擎独立运行）

```plain
┌────────────────────────────────────────────────────────────────────┐
│                    渲染引擎 (C++)  — 独立可执行程序                  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                       核心引擎层                              │  │
│  │        模型管理器  │  动画控制器  │  参数引擎                  │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                     交互处理层                                │  │
│  │               点击检测  │  拖拽处理                            │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              Cubism Native SDK (Core + Framework)              │  │
│  │      模型加载 │ 动作/表情 │ 物理演算 │ 眨眼/呼吸 │ 渲染器     │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                   渲染层 (GLEW + GLFW)                        │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────┘
```

### 2.2 完整架构（Phase 2+ 已实现）

```plain
┌────────────────────────────────────────────────────────────────────┐
│                    控制面板 (Java)                                  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                        UI层 (JavaFX)                          │  │
│  │      主界面  │  设置面板  │  托盘图标  │  宠物管理             │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                       业务逻辑层                              │  │
│  │  状态管理器  │  交互处理器  │  定时任务  │  配置管理器          │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                   WebSocket Server                            │  │
│  └──────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬─────────────────────────────────┘
                               ▲
                               │ WebSocket (JSON Protocol)
                               │
┌──────────────────────────────┴─────────────────────────────────────┐
│                    渲染引擎 (C++)                                  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                   WebSocket Client                            │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                       核心引擎层                              │  │
│  │  模型管理器  │  动画控制器  │  参数引擎  │  音频播放器(Phase 3) │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                     交互处理层                                │  │
│  │          点击检测  │  拖拽处理  │  事件上报                    │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │              Cubism Native SDK (Core + Framework)              │  │
│  │      模型加载 │ 动作/表情 │ 物理演算 │ 眨眼/呼吸 │ 渲染器     │  │
│  └──────────────────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                   渲染层 (GLEW + GLFW)                        │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────┘
```

> **Cubism SDK 说明**：SDK 由闭源 Core 库（C 接口，负责 .moc3 解析和顶点计算）与开源 Framework（C++ 框架，提供模型管理、动作播放、物理演算、OpenGL 渲染等高级功能）两层组成。本项目 Framework 编译为静态库链接，Core 使用 Linux x86_64 预编译静态库。渲染引擎源码基于 SDK 的 `LApp*` 示例代码风格开发（复用 Samples/Common 的基础实现）。SDK 详细集成架构见 [Cubism SDK 集成架构](./renderer/cubism-sdk.md)，版本与许可详见 [工程化](./engineering/README.md)。

---

## 三、技术栈总览

| 组件 | 语言 | 版本 | 关键依赖 | 阶段 |
|:---|:---|:---|:---|:---|
| 渲染引擎 | C++17 | GCC ≥ 11.4 / MinGW-w64 (GCC 13+) / CMake ≥ 3.22 | Cubism Native SDK 5-r.5-beta.3.1（兼容 Cubism 5/5.3）、GLFW 3.4、GLEW 2.2.0（OpenGL 后端）、Vulkan SDK（Vulkan 后端） | ✅ MVP + Phase 2.x |
| 通信模块（渲染引擎端） | C++ | 同上 | IXWebSocket 11.4.6（Client）、nlohmann/json 3.12.0 | ✅ Phase 1 |
| 控制面板 | Java 21 LTS | OpenJDK 21 / Maven ≥ 3.9 | JavaFX 21 (OpenJFX 21.0.5)、Java-WebSocket 1.6.0（Server）、Gson 2.13.2、SLF4J 2.0.17 + Logback 1.5.32、protobuf-java 4.29.3 | ✅ Phase 2 + Phase 3a |
| 音频模块（渲染器侧） | C++ | 同上 | miniaudio（single-header vendored）、libogg 1.3.5 + libvorbis 1.3.7（CMake FetchContent） | ✅ Phase 3b 渲染器侧已实现，控制器侧待实现 |
| 控制面板（Qt 6） | C++17 | Qt 6.10 / MinGW 13.1.0 / CMake ≥ 3.22 / Ninja | Qt6（Core, Gui, Widgets, Network, WebSockets, Qml, Quick, QuickControls2, Concurrent, Charts, Test）、spdlog 1.15.0（CMake FetchContent） | ✅ Phase 5-9 已完成（controller_qt） |
| 通信协议 | — | — | WebSocket（端口 9001）+ JSON Envelope | ✅ Phase 1 |
| 目标平台 | — | — | Ubuntu 22.04 LTS（X11）+ Windows 10+（Win32 / MinGW）；GPU 支持 OpenGL 3.3+ 或 Vulkan 1.2+（Vulkan 后端） | ✅ MVP + Windows 扩展 |

> 完整版本与选型决策详见 [工程化](./engineering/README.md)。

---

## 四、设计原则

### 4.1 MVP 阶段：渲染引擎独立运行

MVP 阶段渲染引擎作为独立可执行程序运行，自行完成全部功能：

- 硬编码模型路径，启动时直接加载
- 点击检测后直接触发动画反馈（无需外部调度）
- 闲时动作由引擎内置定时器随机触发
- 拖拽移动由引擎直接处理窗口位置

### 4.2 Phase 1：通信机制

- 采用 WebSocket 作为进程间通信协议
- 使用 JSON 格式的结构化消息，统一 Envelope 格式（详见 [通信协议](./protocol/README.md)）
- 支持指令（控制面板→渲染器）和事件（渲染器→控制面板）双向通信
- 关键指令支持 request-response 回执确认

### 4.3 Phase 2：分离关注点

引入控制面板后，职责分离为：

- **渲染引擎 (C++)** ：专注于高性能图形渲染、模型加载、动画播放、用户输入检测
- **控制面板 (Java)** ：专注于业务逻辑、状态管理、用户配置和系统托盘交互

### 4.4 Phase 3：音频模块

音频模块在控制面板之后实现，采用**音频文件与模型文件分离管理**的设计：

- **独立存储**：音频文件存放在独立目录（`audio/`），不嵌入模型目录，降低存储占用
- **跨模型复用**：多个模型可引用同一音频文件，通过控制面板配置映射关系
- **渲染器播放**：音画同步要求高，音频由渲染器侧 `AudioManager`（miniaudio + libvorbis）播放 OGG，已实现 `play_audio`/`stop_audio`/`set_volume` 指令
- **控制面板管理**：音频文件的导入、映射、音量控制由控制面板 UI 管理（`AudioMappingManager` 与 UI 待实现）

### 4.5 职责边界（Phase 2+ 完整架构）

| 职责      | 控制面板 | 渲染引擎     |
| :------ | :--- | :------- |
| 模型加载与管理 | ✗    | ✓        |
| 动画渲染    | ✗    | ✓        |
| 点击/拖拽检测 | ✗    | ✓ (底层)   |
| 音频播放    | ✗    | ✓ (同步播放) |
| 音量控制/静音 | ✓    | ✓ (响应指令) |
| 业务状态管理  | ✓    | ✗        |
| 用户配置界面  | ✓    | ✗        |
| 定时任务    | ✓    | ✗        |
| 动作触发逻辑  | ✓    | ✗        |

---

## 五、设计决策总结

| 方面       | 设计决策                    | 当前状态 | 阶段 |
| :------- | :---------------------- | :--- | :---: |
| **通信**   | WebSocket + JSON 协议，Envelope 格式，端口 9001 | ✅ 已实现 | Phase 1 |
| **整体架构** | Java 控制面板（Server）+ C++ 渲染引擎（Client）分离 | ✅ 已实现 | Phase 2 |
| **控制面板 UI** | Tab 式布局（Dashboard/Settings/Actions/Advanced），支持多宠物实例管理 | ✅ 已实现 | Phase 2 |
| **多实例** | 每个宠物实例独立配置（`instances/{uuid}.json`），面板统一管理（`panel.json`） | ✅ 已实现 | Phase 2 |
| **模型加载** | 控制面板通过 `load_model` 指令动态切换（模型短名称，如 "Hiyori"） | ✅ 已实现 | Phase 2 |
| **点击事件** | 渲染器即时反馈 + 上报 `hit` 事件到控制面板处理业务逻辑 | ✅ 已实现 | Phase 2 |
| **拖拽行为** | 直接跟随模式，`drag_end` 事件上报窗口位置（window_x, window_y），控制面板持久化 | ✅ 已实现 | Phase 2 |
| **闲时行为** | 控制面板 Scheduler 定时触发，从闲时动作池随机选择，预留权重打分扩展 | ✅ 已实现 | Phase 2 |
| **语音包挂载** | 语音包与模型解耦挂载，meta.mko (Protobuf) 解析，Java 侧行为引擎 + 渲染器 `play_motion_ext` 均已实现 | ✅ Java 侧 + 渲染器侧均已实现 | Phase 3a |
| **渲染后端** | OpenGL/Vulkan 双后端，`IGraphicsBackend` 抽象接口（6 方法），`OpenGLBackend` + `VulkanBackend` 双实现，编译时开关 `USE_VULKAN` 切换（无运行时切换） | ✅ 已实现 | Phase 2.x |
| **音频播放** | 音频文件独立于模型管理，支持跨模型复用；渲染器侧 miniaudio + libvorbis 播放 OGG（`play_audio`/`stop_audio`/`set_volume`），控制面板管理映射和音量 | ⚠️ 渲染器侧 ✅ 已实现，控制器侧（`AudioMappingManager`/UI）待实现 | Phase 3b |
| **Qt 控制面板** | Qt 6.10 / C++17 / QML，JavaFX 控制面板的 C++ 后继。多实例管理、完整协议覆盖（25 命令 + 14 事件）、每实例配置持久化（QSaveFile 原子写入）、闲时调度、崩溃恢复（指数退避）、系统托盘、开机自启、资源监视器（QtCharts）、语音包挂载（手写 protobuf）、字幕系统、布局同步 | ✅ Phase 5-9 已完成（38 QTest 全部通过） | Phase 5-9 |
| **口型同步** | lipSync txt 解析 + 定时驱动 `ParamMouthOpenY`，文案气泡 UI | ❌ 待实现 | Phase 3c |
| **性能**   | 自适应帧率（15-60fps）或固定帧率（15-120fps 可配），闲时低占用 | ✅ 已实现 | MVP/Phase 2 |
| **平台**   | Ubuntu 22.04 / X11 + Windows 10+ / Win32（MinGW Makefiles） | ✅ 双平台已实现 | MVP + Windows 扩展 |
| **容错**   | 崩溃自动重启（指数退避，最大 5 次），断连缓存关键指令 | ✅ 已实现 | Phase 2 |
| **配置**   | JSON 格式，多文件分层：`config.json`（全局）、`panel.json`（面板）、`instances/*.json`（实例）、`mount.json`（挂载） | ✅ 已实现 | Phase 2/3a |
| **分发**   | 单一 C++ 可执行文件 | 当前状态 | MVP |
| **日志**   | C++ 端使用 `LAppPal::PrintLogLn`（SDK 内置），Java 端使用 SLF4J + Logback（文件轮转） | ✅ 已实现 | MVP/Phase 2 |
| **扩展预留** | Lua 脚本插件系统、养成状态系统 | 架构预留 | 后期 |

---

## 六、文档索引

| 文档 | 内容 | 实现状态 |
|:---|:---|:---:|
| [渲染引擎设计](./renderer/README.md) | C++ 渲染引擎模块详细设计 | ✅ MVP + Phase 1 + Phase 2.x（Vulkan 后端）已实现 |
| [Cubism SDK 集成](./renderer/cubism-sdk.md) | Cubism SDK 集成架构、关键 API | ✅ MVP 已实现 |
| [音频播放架构](./renderer/audio.md) | 音频模块架构设计 | ✅ 渲染器侧已实现（miniaudio + libvorbis），控制器侧待实现 |
| [渲染后端解耦](./renderer/opengl-decoupling.md) | OpenGL/Vulkan 双后端解耦方案与实现（`IGraphicsBackend` 抽象） | ✅ Phase 2.x 已实现 |
| [性能设计](./renderer/performance.md) | 自适应帧率、资源优化策略 | ✅ MVP 已实现 |
| [控制面板设计](./controller/README.md) | Java 控制面板模块详细设计、多实例管理、语音包挂载、闲时行为策略 | ✅ Phase 2 + Phase 3a 已实现 |
| [通信协议](./protocol/README.md) | WebSocket 协议规范、消息格式 | ✅ Phase 1 已实现 |
| [协议 - 接口规格](./protocol/interface.md) | 自包含完整接口规格：类型约定 + 全部命令/事件字段表 + 交互模式 | ✅ Phase 1 已实现 |
| [协议 - Commands](./protocol/commands.md) | 控制面板→渲染器指令定义 | ✅ Phase 1 已实现 |
| [协议 - Events](./protocol/events.md) | 渲染器→控制面板事件定义 | ✅ Phase 1 已实现 |
| [协议 - 握手流程](./protocol/handshake.md) | 连接建立与断连恢复流程 | ✅ Phase 1 已实现 |
| [协议 - 错误码](./protocol/error-codes.md) | 错误码体系 | ✅ Phase 1 已实现 |
| [协议 - 实现参考](./protocol/implementation.md) | 断连缓存策略、双端关键接口 | ✅ Phase 1/2 已实现 |
| [交互设计](./interaction/README.md) | 点击事件处理流程、拖拽行为设计 | ✅ 已实现 |
| [系统设计](./system/README.md) | 系统设计概述索引 | ✅ 核心已实现 |
| [容错与错误处理](./system/fault-tolerance.md) | 崩溃恢复、断连处理 | ✅ Phase 2 已实现 |
| [配置文件设计](./system/configuration.md) | config.json、panel.json、instances/*.json、mount.json、hit_area_cache.json | ✅ Phase 2/3a 已实现 |
| [日志体系](./system/logging.md) | 日志框架、级别、文件管理 | ✅ MVP/Phase 2 已实现 |
| [启动流程](./system/startup.md) | MVP 和控制面板启动/关闭流程、多实例管理 | ✅ 已实现 |
| [扩展性预留](./system/extensibility.md) | 插件系统、状态系统预留 | 架构预留 |
| [外置语音包挂载](./system/voice-pack-mounting.md) | 语音包与模型解耦挂载设计、事件映射、口型同步 | ✅ Phase 3a Java 侧已实现，渲染器侧待实现 |
| [工程化](./engineering/README.md) | 工程化概述索引 | ✅ 持续更新 |
| [开发语言与工具链](./engineering/toolchain.md) | C++/Java 工具链选型 | ✅ 已确定 |
| [第三方库选型](./engineering/dependencies.md) | C++/Java 端依赖库 | ✅ 已确定 |
| [构建与分发](./engineering/build.md) | 构建命令、打包结构 | ✅ 已确定 |
| [项目目录结构](./engineering/project-structure.md) | 渲染引擎/控制面板/完整目录结构 | ✅ 持续更新 |
| [开发环境与代码规范](./engineering/coding-standards.md) | IDE、代码风格、Git 规范 | ✅ 已确定 |
| [测试策略](./engineering/testing.md) | 单元测试、集成测试、端到端测试 | ✅ 持续更新 |
| [控制面板技术栈调研](./research/control-panel-tech-stack.md) | 下一代控制面板技术栈选型调研（Qt/Avalonia/Slint/Flutter/Compose MP/GTK4 对比） | ✅ 2026-07 调研完成 |
| [控制面板分步开发方案](./controller/development-plan.md) | 10 阶段开发路线图（技术栈无关，含 Qt/Slint 实现要点） | ✅ 2026-07 规划完成 |
| `controller_qt/README.md` | Qt 6 控制面板完整文档（Phase 5-9：特性、构建、打包、测试、架构、QML 页面、协议互通） | ✅ Phase 5-9 已完成 |
