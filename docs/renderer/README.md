# 渲染引擎设计 (C++)

> 本文档描述 C++ 渲染引擎的模块设计。
> 整体架构参见 [架构总览](../README.md)。
>
> **实现状态**：MVP（独立运行）、Phase 1（WebSocket 通信）、Phase 2.x（OpenGL/Vulkan 双后端解耦）、Phase 3b 渲染器侧音频（AudioManager）均已完整实现。渲染引擎源码基于 Cubism SDK 的 `LApp*` 示例代码风格开发，复用 Samples/Common 的基础实现。
>
> **相关文档**：[Cubism SDK 集成](./cubism-sdk.md) | [音频播放架构](./audio.md) | [OpenGL/Vulkan 后端解耦方案](./opengl-decoupling.md) | [性能设计](./performance.md)

---

## 一、模块概览

> **代码结构说明**：渲染引擎源码遵循 Cubism SDK 的 `LApp*` 命名风格。下方列出各模块对应的实际源文件。

### 1.1 核心引擎层 — LAppDelegate（单例）

负责整个渲染引擎的生命周期管理，协调各子系统工作。包含主循环控制（`Run()`）和优雅关闭。`LAppDelegate` 持有 `WindowManager`（GLFW 窗口）、`IGraphicsBackend`（图形后端，编译期选择 OpenGL 或 Vulkan）、`AudioManager`（音频引擎）等子系统指针，是整个引擎的组装枢纽。使用 `glfwWaitEventsTimeout(1.0/30.0)` 实现闲时低 CPU 占用。

| 源文件 | 职责 |
|:---|:---|
| `LAppDelegate.cpp/.hpp` | 引擎主类（单例），管理主循环、事件分发、子系统生命周期 |
| `LAppDefine.cpp/.hpp` | 全局常量定义（窗口尺寸、WebSocket 端口 9001、资源路径等） |
| `LAppPal.cpp/.hpp` | 平台抽象层（文件 I/O、时间获取、日志输出 `PrintLogLn`） |
| `AudioManager.cpp/.hpp` | miniaudio + libvorbis 音频播放引擎（OGG 解码播放，详见 [音频播放架构](./audio.md) 与本文档 1.7 音频层） |

### 1.2 模型管理器 — LAppLive2DManager（单例）

负责 Live2D 模型的加载和生命周期管理。通过 Cubism Native SDK 解析模型文件（.model3.json），加载纹理、动作、表情等资源。支持模型切换（`ChangeScene()` 接收模型短名称，如 `"Hiyori"`，自动从 `Resources/` 目录加载）。不存在独立的"卸载"操作——模型切换时隐含卸载旧模型。

| 源文件 | 职责 |
|:---|:---|
| `LAppLive2DManager.cpp/.hpp` | 模型加载/切换/释放、场景管理 |
| `LAppModel.cpp/.hpp` | 单个模型封装（继承 `CubismUserModel`，管理动作/表情/物理/HitArea） |
| `LAppTextureManager.cpp/.hpp` | 纹理加载与缓存（stb_image 解码 PNG → OpenGL 纹理 或 Vulkan `CubismImageVulkan`，由编译期后端选择决定） |

> **模型路径约定**：`ChangeScene(modelName)` 期望模型目录结构为 `Resources/<modelName>/<modelName>.model3.json`。控制面板发送 `load_model` 指令时传递模型短名称（如 `"Hiyori"`），而非完整路径。

### 1.3 渲染后端抽象层（Phase 2.x ✅）

渲染引擎通过 `IGraphicsBackend` 接口抽象图形后端，支持 **OpenGL** 与 **Vulkan** 双实现。设计目标是让上层（`LAppDelegate`、`LAppTextureManager`）不直接依赖具体图形 API。完整的耦合分析与分阶段实施记录见 [OpenGL/Vulkan 后端解耦方案](./opengl-decoupling.md)。

**关键设计决策**：
- **编译时切换，无运行时切换**。通过 CMake 选项 `-DUSE_VULKAN=ON` 选择后端，宏 `USE_VULKAN` 控制条件编译。桌面宠物场景不需要运行时切换渲染后端。
- **`CUBISM_RENDERER_TYPE` 宏** 选择 Cubism SDK 的渲染器子类（`CubismRenderer_OpenGLES2` 或 `CubismRenderer_Vulkan`），与 SDK 自身的 `FRAMEWORK_SOURCE` 编译期分支一致。
- **GLFW 窗口管理共享**：`platform/WindowManager.cpp` 封装 GLFW 窗口操作，被 OpenGL 与 Vulkan 两条路径共同复用（详见本文档 1.5 窗口管理层）。

**接口定义**（`graphics/IGraphicsBackend.hpp`，6 个虚方法）：

| 方法 | 职责 | OpenGL 实现 | Vulkan 实现 |
|:---|:---|:---|:---|
| `InitializeGraphics(window)` | 初始化图形上下文 | `glewInit` + GL 状态 + PBO | Instance → Device → Swapchain → CommandPool |
| `ReleaseGraphics()` | 释放图形资源 | GL 对象销毁 | Vulkan 对象销毁 |
| `BeginFrame(w, h)` | 每帧开始 | `glViewport` + `glClear` | `vkAcquireNextImageKHR` + 动态渲染 `vkCmdBeginRendering` |
| `EndFrame(window)` | 每帧结束 | `glfwSwapBuffers` | Submit + `vkQueuePresentKHR` |
| `CreateTexture(data,w,h,ch)` | 创建并上传纹理 | `glGenTextures` + `glTexImage2D` | 返回 0（有意设计，见下方说明） |
| `DeleteTexture(handle)` | 删除纹理 | `glDeleteTextures` | 空实现（同上原因） |
| `IsPixelTransparent(x,y,h)` | 像素回读（点击穿透） | PBO + `glReadPixels` | `vkCmdCopyImageToBuffer` → map → 读 alpha |

| 源文件 | 职责 |
|:---|:---|
| `graphics/IGraphicsBackend.hpp` | 渲染后端抽象接口（6 虚方法） |
| `graphics/OpenGLBackend.cpp/.hpp` | OpenGL 实现（从 `LAppDelegate` 迁移的 GL 调用） |
| `graphics/VulkanBackend.cpp/.hpp` | Vulkan 实现（979 行，合并 `VulkanManager` + `SwapchainManager`；动态渲染 `vkCmdBeginRendering`，无 RenderPass/Framebuffer；像素回读 `vkCmdCopyImageToBuffer`） |

> **Vulkan 纹理路径说明**：Vulkan 后端的纹理创建由 `LAppTextureManager::CreateTextureFromPngFile(VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, float)` 重载直接处理，生成 `CubismImageVulkan`（SDK RAII 类，含 `VkImage` + `VkDeviceMemory` + `VkImageView` + `VkSampler`），不经过 `IGraphicsBackend::CreateTexture()`。因此 `VulkanBackend` 的 `CreateTexture` 返回 0 / `DeleteTexture` 空实现是**有意设计**，并非未完成的 stub。

### 1.4 视图与交互层 — LAppView

处理坐标变换（屏幕坐标 → 模型本地坐标）、鼠标事件分发和渲染调度。通过 Cubism SDK 的 HitTest 功能检测用户点击了模型的哪个区域（如 Head、Body 等）。支持窗口拖拽移动（详见 [交互设计](../interaction/README.md)）。

| 源文件 | 职责 |
|:---|:---|
| `LAppView.cpp/.hpp` | 视图层（坐标变换、触摸/点击事件→模型坐标、渲染调用） |
| `LAppDelegate.cpp` | 鼠标回调注册、拖拽状态管理（`_isDragging` 标志） |

> **点击处理**：渲染器检测到 HitArea 命中后，直接播放即时反馈动画，同时通过 EventEmitter 上报 `hit` 事件到控制面板。

### 1.5 窗口管理层

使用 GLFW 创建无边框、透明背景、始终置顶的窗口。GLFW 窗口管理代码封装在 `platform/WindowManager.cpp`，被 OpenGL 与 Vulkan 两条渲染路径共享。

**平台支持策略**：
- **当前**：Ubuntu (X11) + Windows (Win32)，通过 `platform/WindowManager.cpp` 抽象平台差异（Win32 侧含 `WS_EX_TOOLWINDOW` / `WS_EX_TOPMOST` 等桌面宠物专属窗口样式处理）
- **后期扩展**：macOS（NSWindow）、Wayland

> 注意：Wayland 对"始终置顶"和"透明背景"的支持存在限制，暂不纳入。

### 1.6 通信层 (Network Layer) ✅ Phase 1 已实现

提供 WebSocket 客户端，启动后主动连接控制面板的 WebSocket 服务端（端口 9001）。接收控制面板下发的指令并解析执行，将渲染器事件上报给控制面板。

| 源文件 | 职责 |
|:---|:---|
| `network/WebSocketClient.cpp/.hpp` | IXWebSocket 客户端封装（连接/断连/消息收发） |
| `network/Protocol.cpp/.hpp` | Envelope 协议序列化/反序列化（nlohmann/json），含 `generateId()`、`createCommand()`/`createEvent()`/`createResponse()` 工厂方法 |
| `network/MessageHandler.cpp/.hpp` | 消息路由（按 `action` 分发 command，过滤 response，未知 action 返回错误码 5003） |
| `network/CommandHandlers.cpp/.hpp` | 指令处理器注册（共 20 条指令，见下表） |
| `network/EventEmitter.cpp/.hpp` | 事件上报（`ready`、`model_loaded`/`model_load_failed`、`motion_started`/`motion_finished`、`hit`、`drag_start`/`drag_end`、`layout_changed`、`window_resized`、`layout_state`、`stats_state`、`error`，共 13 条） |

**已注册指令（20 条）**：

| 指令 | 功能 | 备注 |
|:---|:---|:---|
| `hello` | 握手心跳 | 连接建立确认 |
| `load_model` | 加载/切换模型 | 错误码 1001 |
| `play_motion` | 播放内置动作（组+索引） | 错误码 2001（无模型） |
| `play_motion_ext` | 播放外置语音包动作 | Phase 3a，错误码 3001/3002/3003/3004 |
| `stop_motion` | 停止当前动作 | — |
| `set_expression` | 设置表情 | — |
| `set_position` | 设置模型在窗口内位置 | — |
| `set_scale` | 设置模型缩放 | **⚠️ stub（仅日志，未实现缩放）** |
| `set_size` | 设置窗口尺寸 | 错误码 4004 |
| `set_opacity` | 设置窗口透明度 | — |
| `set_hit_areas` | 设置命中区域 | 错误码 1005 |
| `set_fps` | 设置帧率（0=自适应，1-120=固定） | 错误码 6003 |
| `play_audio` | 播放音频文件 | Phase 3b，错误码 7001/7002/7003/7004 |
| `stop_audio` | 停止所有音频 | Phase 3b |
| `set_volume` | 设置主音量 | Phase 3b，错误码 7002 |
| `set_layout` | 设置模型布局参数 | 错误码 8001 |
| `get_layout` | 查询模型布局参数 | 错误码 8001 |
| `reset_layout` | 重置布局到默认 | — |
| `get_stats` | 请求资源占用快照 | 经 `stats_state` 事件回传（不走 Response） |
| `shutdown` | 关闭渲染器 | — |

> **线程安全**：WebSocket 回调在后台线程执行，**禁止**在回调中直接调用 OpenGL/Vulkan API。指令通过消息队列传递到主线程处理（每帧最多处理 50 条，队列上限 1000 条），`glfwPostEmptyEvent()` 用于唤醒主循环。

### 1.7 音频层（Phase 3b 渲染器侧 ✅）

渲染器侧音频播放由 `AudioManager` 实现，负责音效的加载、解码、播放与音量控制。音频文件与模型文件**分离管理**，支持跨模型复用（设计详见 [音频播放架构](./audio.md)）。

**实现技术栈**：
- **miniaudio**（单头文件）— 音频引擎，提供设备管理、混音、音量控制
- **libvorbis + libogg**（CMake FetchContent）— OGG/Vorbis 解码，**替代原设计的 OpenAL 方案**（更轻量，单文件集成，无需额外系统依赖）

| 源文件 | 职责 |
|:---|:---|
| `AudioManager.cpp/.hpp` | 音频引擎封装（`ma_engine` + `ma_sound` + `ma_decoder`） |

**`AudioManager` 接口**：

| 方法 | 职责 |
|:---|:---|
| `Init()` / `Uninit()` | 初始化 / 销毁 miniaudio 引擎 |
| `Play(filePath, volume)` | 播放音频文件（内部用 `ManagedSound` 管理活跃音效：fileData + decoder + sound） |
| `StopAll()` | 停止所有活跃音效 |
| `SetVolume(v)` / `GetVolume()` | 主音量设置 / 查询 |
| `SetMuted(b)` / `IsMuted()` | 静音控制 |
| `IsInitialized()` | 引擎就绪状态 |
| `CleanupFinishedSounds()` | 清理已播放完毕的 `ManagedSound`（内部调用） |

**命令接入**（`CommandHandlers.cpp` 注册）：

| 指令 | 错误码 | 说明 |
|:---|:---|:---|
| `play_audio` | 7001（path required）/ 7002（engine not init）/ 7003（file not found） | 播放指定路径的 OGG 文件 |
| `stop_audio` | — | 停止全部活跃音效 |
| `set_volume` | 7002（engine not init） | 设置主音量 |

> **生命周期**：`LAppDelegate` 持有 `_audioManager` 成员，初始化时 `new AudioManager()` 并调用 `Init()`。线程安全通过内部 `std::mutex` 保护。
>
> **控制器侧待实现**：音频映射配置管理（模型+动作 → 音频文件路径）、音频管理 UI、音量滑块等仍由 Java 控制面板侧负责，目前待实现。
