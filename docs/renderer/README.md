# 渲染引擎设计 (C++)

> 本文档描述 C++ 渲染引擎的模块设计。
> 整体架构参见 [架构总览](../README.md)。
>
> **实现状态**：MVP（独立运行）和 Phase 1（WebSocket 通信）已完成。渲染引擎源码基于 Cubism SDK 的 `LApp*` 示例代码风格开发，复用 Samples/Common 的基础实现。本文档中标注「Phase 3」的模块待后续实现。
>
> **相关文档**：[Cubism SDK 集成](./cubism-sdk.md) | [音频播放架构](./audio.md) | [性能设计](./performance.md)

---

## 一、模块概览

> **代码结构说明**：渲染引擎源码遵循 Cubism SDK 的 `LApp*` 命名风格。下方列出各模块对应的实际源文件。

### 1.1 核心引擎层 — LAppDelegate（单例）

负责整个渲染引擎的生命周期管理，协调各子系统工作。包含 GLFW 窗口创建、OpenGL 上下文初始化、主循环控制（`Run()`）和优雅关闭。使用 `glfwWaitEventsTimeout(1.0/30.0)` 实现闲时低 CPU 占用。

| 源文件 | 职责 |
|:---|:---|
| `LAppDelegate.cpp/.hpp` | 引擎主类（单例），管理窗口、主循环、事件分发 |
| `LAppDefine.cpp/.hpp` | 全局常量定义（窗口尺寸、WebSocket 端口 9000、资源路径等） |
| `LAppPal.cpp/.hpp` | 平台抽象层（文件 I/O、时间获取、日志输出 `PrintLogLn`） |

### 1.2 模型管理器 — LAppLive2DManager（单例）

负责 Live2D 模型的加载和生命周期管理。通过 Cubism Native SDK 解析模型文件（.model3.json），加载纹理、动作、表情等资源。支持模型切换（`ChangeScene()` 接收模型短名称，如 `"Hiyori"`，自动从 `Resources/` 目录加载）。不存在独立的"卸载"操作——模型切换时隐含卸载旧模型。

| 源文件 | 职责 |
|:---|:---|
| `LAppLive2DManager.cpp/.hpp` | 模型加载/切换/释放、场景管理 |
| `LAppModel.cpp/.hpp` | 单个模型封装（继承 `CubismUserModel`，管理动作/表情/物理/HitArea） |
| `LAppTextureManager.cpp/.hpp` | 纹理加载与缓存（stb_image 解码 PNG → OpenGL 纹理绑定） |

> **模型路径约定**：`ChangeScene(modelName)` 期望模型目录结构为 `Resources/<modelName>/<modelName>.model3.json`。控制面板发送 `load_model` 指令时传递模型短名称（如 `"Hiyori"`），而非完整路径。

### 1.3 视图与交互层 — LAppView

处理坐标变换（屏幕坐标 → 模型本地坐标）、鼠标事件分发和渲染调度。通过 Cubism SDK 的 HitTest 功能检测用户点击了模型的哪个区域（如 Head、Body 等）。支持窗口拖拽移动（详见 [交互设计](../interaction/README.md)）。

| 源文件 | 职责 |
|:---|:---|
| `LAppView.cpp/.hpp` | 视图层（坐标变换、触摸/点击事件→模型坐标、渲染调用） |
| `LAppDelegate.cpp` | 鼠标回调注册、拖拽状态管理（`_isDragging` 标志） |

> **点击处理**：渲染器检测到 HitArea 命中后，直接播放即时反馈动画，同时通过 EventEmitter 上报 `hit` 事件到控制面板。

### 1.4 窗口管理层

使用 GLFW 创建无边框、透明背景、始终置顶的窗口。

**平台支持策略**：
- **当前**：Ubuntu 桌面版，仅支持 X11 窗口系统
- **后期扩展**：Windows（DWM）、macOS（NSWindow）、Wayland

> 注意：Wayland 对"始终置顶"和"透明背景"的支持存在限制，暂不纳入。

### 1.5 通信层 (Network Layer) ✅ Phase 1 已实现

提供 WebSocket 客户端，启动后主动连接控制面板的 WebSocket 服务端（端口 9000）。接收控制面板下发的指令并解析执行，将渲染器事件上报给控制面板。

| 源文件 | 职责 |
|:---|:---|
| `network/WebSocketClient.cpp/.hpp` | IXWebSocket 客户端封装（连接/断连/消息收发） |
| `network/Protocol.cpp/.hpp` | Envelope 协议序列化/反序列化（nlohmann/json），含 `generateId()`、`createCommand()`/`createEvent()`/`createResponse()` 工厂方法 |
| `network/MessageHandler.cpp/.hpp` | 消息路由（按 `action` 分发 command，过滤 response，未知 action 返回错误码 5003） |
| `network/CommandHandlers.cpp/.hpp` | 指令处理器注册（`load_model`、`play_motion`、`stop_motion`、`set_expression`、`set_position`、`set_scale`、`set_opacity`、`shutdown`） |
| `network/EventEmitter.cpp/.hpp` | 事件上报（`hit`、`drag_start`/`drag_end`、`model_loaded`/`model_load_failed`、`motion_started`/`motion_finished`、`ready`） |

> **线程安全**：WebSocket 回调在后台线程执行，**禁止**在回调中直接调用 OpenGL API。指令通过消息队列传递到主线程处理（每帧最多处理 50 条，队列上限 1000 条），`glfwPostEmptyEvent()` 用于唤醒主循环。
