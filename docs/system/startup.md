# 启动流程

> 系统设计概述参见 [系统设计](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、MVP 启动流程（渲染引擎独立运行）

```plain
1. Renderer (C++) 启动
   │
   ├─> 2. 初始化 GLFW/GLEW
   │
   ├─> 3. Cubism SDK 初始化
   │      ├─> 创建 ICubismAllocator 实现（自定义内存分配器）
   │      ├─> CubismFramework::CubismStartUp（注册分配器 + 日志回调）
   │      ├─> CubismFramework::Initialize（初始化 Framework 内部状态）
   │      └─> 记录 SDK 版本（csmGetVersion）
   │
   ├─> 4. 创建透明窗口 (X11/Win32，由 WindowManager 抽象)
   │
   ├─> 5. 加载硬编码模型路径
   │      │
   │      ├─ 成功 → 继续
   │      └─ 失败 → 输出错误日志，退出
   │
   └─> 6. 进入主循环（渲染 + 交互 + 闲时定时器）
```

> **平台支持**：原始 MVP 面向 Ubuntu 22.04 / X11，当前已支持 Windows（MinGW，win32）。窗口创建、透明无边框置顶等平台相关逻辑由 `renderer/src/platform/WindowManager.cpp` 抽象，X11 与 Win32 实现对上层主流程透明。

---

## 二、控制面板启动流程 ✅ 已实现（MainWindowController 编排）

```plain
1. App.java (JavaFX Application) 启动
   │
   ├─> 2. start() 中创建 MainWindowController 实例
   │
   ├─> 3. MainWindowController.startup() 开始编排：
   │      │
   │      ├─> 3a. ConfigManager.load() 加载配置（~/.config/desktop-pet/config.json）
   │      │       └─ 首次运行 → 创建默认配置文件
   │      │       └─ JSON 损坏 → 使用默认值，WARN 日志
   │      │
   │      ├─> 3b. PetWebSocketServer 启动（监听端口 9001）
   │      │
   │      ├─> 3c. 注册事件处理器到 MessageDispatcher
   │      │       ├─ ready → 发送 load_model + set_position
   │      │       ├─ model_loaded → ModelInfoParser 解析模型 → 启动 Scheduler
   │      │       ├─ hit → InteractionHandler → play_motion 指令
   │      │       ├─ drag_start → 设置 isDragging = true
   │      │       ├─ drag_end → isDragging guard → 持久化窗口位置到 ConfigManager
   │      │       ├─ motion_started / motion_finished → 更新 PetState
   │      │       └─ error → 日志记录
   │      │
   │      ├─> 3d. ProcessManager.startRenderer() 启动渲染器进程
   │      │       └─ 注册 exitCallback（非零退出码 → scheduleRestart）
   │      │
   │      └─> 3e. 等待渲染器连接和 ready 事件（异步）
   │
   ├─> 4. 渲染器初始化（同 MVP 步骤 2-4）
   │      └─> 作为 WebSocket Client 连接 ws://localhost:9001
   │
   ├─> 5. 渲染器发送 ready 事件
   │      │
   │      ├─> MainWindowController 发送 load_model（配置中的当前模型短名称）
   │      ├─> MainWindowController 发送 set_position（配置中的窗口位置）
   │      └─> flushPendingCommands()（重发缓存指令）
   │
   └─> 6. load_model 成功回执 → ModelInfoParser 解析模型 → Scheduler.start() → 正常运行
```

---

## 三、多实例管理 ✅ 已实现

Phase 2 引入多实例管理后，配置采用分层结构：

```plain
启动流程：
  PanelStateManager.load()
    │
    ├─ 读取 panel.json → PanelConfig（面板窗口位置/主题/实例 ID 列表）
    │   └─ 若不存在，检查旧版 panel-state.json → 自动迁移
    │
    ├─ 遍历 instanceIds
    │   └─ InstanceConfigManager.load(uuid) → InstanceConfig
    │       └─ 创建 PetInstance（JavaFX 可观察模型，绑定 UI）
    │
    └─ 每个 PetInstance 独立管理：
        ├─ 独立渲染器进程（ProcessManager）
        ├─ 独立 WebSocket 连接
        ├─ 独立 Scheduler（闲时动作）
        └─ 独立语音包挂载（MountedBehaviorEngine）

> **渲染器选择与 graphicsBackend**：每个实例启动渲染器进程时，`ProcessManager` 经由 `MainWindowController.resolveRendererPath(backend)` 依据实例配置的 `graphics_backend`（`instances/{uuid}.json`，取值 `opengl` / `vulkan`）解析对应的渲染器可执行文件路径；若实例未指定则回退到全局 `config.json` 的 `system.default_graphics_backend`。OpenGL 与 Vulkan 为编译期切换（`-DUSE_VULKAN=ON`），无运行时切换。

配置文件结构：
  ~/.config/desktop-pet/
  ├── panel.json                  ← 面板配置
  ├── instances/{uuid-1}.json     ← 实例 1 配置
  ├── instances/{uuid-2}.json     ← 实例 2 配置
  └── mount.json                  ← 语音包挂载关系
```

---

## 四、关闭流程（MainWindowController.shutdown()）

```plain
App.stop() → MainWindowController.shutdown()
   │
   ├─> 1. Scheduler.shutdown() — 停止闲时动作触发
   ├─> 2. ConfigManager.save() — 持久化当前配置
   ├─> 3. 发送 shutdown 指令到渲染器（等待 response）
   ├─> 4. ProcessManager.stopRenderer() — 优雅停止（5 秒超时后强制终止）
   └─> 5. PetWebSocketServer.stop() — 关闭 WebSocket 服务
```
