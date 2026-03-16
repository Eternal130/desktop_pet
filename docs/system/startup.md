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
   ├─> 4. 创建透明窗口 (X11)
   │
   ├─> 5. 加载硬编码模型路径
   │      │
   │      ├─ 成功 → 继续
   │      └─ 失败 → 输出错误日志，退出
   │
   └─> 6. 进入主循环（渲染 + 交互 + 闲时定时器）
```

---

## 二、控制面板启动流程 ✅ 已实现（AppOrchestrator 编排）

```plain
1. App.java (JavaFX Application) 启动
   │
   ├─> 2. start() 中创建 AppOrchestrator 实例
   │
   ├─> 3. AppOrchestrator.startup() 开始编排：
   │      │
   │      ├─> 3a. ConfigManager.load() 加载配置（~/.config/desktop-pet/config.json）
   │      │       └─ 首次运行 → 创建默认配置文件
   │      │       └─ JSON 损坏 → 使用默认值，WARN 日志
   │      │
   │      ├─> 3b. PetWebSocketServer 启动（监听端口 9000）
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
   │      └─> 作为 WebSocket Client 连接 ws://localhost:9000
   │
   ├─> 5. 渲染器发送 ready 事件
   │      │
   │      ├─> AppOrchestrator 发送 load_model（配置中的当前模型短名称）
   │      ├─> AppOrchestrator 发送 set_position（配置中的窗口位置）
   │      └─> flushPendingCommands()（重发缓存指令）
   │
   └─> 6. load_model 成功回执 → ModelInfoParser 解析模型 → Scheduler.start() → 正常运行
```

---

## 三、关闭流程（AppOrchestrator.shutdown()）

```plain
App.stop() → AppOrchestrator.shutdown()
   │
   ├─> 1. Scheduler.shutdown() — 停止闲时动作触发
   ├─> 2. ConfigManager.save() — 持久化当前配置
   ├─> 3. 发送 shutdown 指令到渲染器（等待 response）
   ├─> 4. ProcessManager.stopRenderer() — 优雅停止（5 秒超时后强制终止）
   └─> 5. PetWebSocketServer.stop() — 关闭 WebSocket 服务
```
