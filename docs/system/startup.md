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

## 二、控制面板启动流程 ✅ 已实现（controller_qt 编排）

```plain
1. controller_qt 进程启动（main.cpp：QGuiApplication + QQmlApplicationEngine，
   注册上下文属性 instanceManager / trayManager / autoLaunch / panelConfig /
   environmentChecker / windowStateSaver，加载 QML 主界面）
   │
   ├─> 2. 加载面板配置与实例列表（~/.config/desktop-pet/ 下的 panel.json
   │      与 instances/{uuid}.json，QSaveFile 原子写入）
   │
   ├─> 3. WsServer 启动（QWebSocketServer 监听 127.0.0.1:9001，
   │      三重门令牌握手，按 instanceId 多实例路由）
   │
   ├─> 4. InstanceManager 为每个实例创建 InstanceSession（每宠物编排器）：
   │      ├─ ProcessManager 启动渲染器子进程（QProcess，经 CLI 参数传递
   │      │    --port / --instance-id / --model / --token / --x / --y / --width / --height）
   │      ├─ MessageDispatcher 路由 Envelope（response / event / command）
   │      ├─ EventRegistry 登记 14 类事件处理（默认日志 + 按实例注册）
   │      └─ RestartController 挂接崩溃恢复（指数退避，最大 5 次）
   │
   ├─> 5. 渲染器初始化（同 MVP 步骤 2-4），作为 WebSocket Client
   │      连接 ws://127.0.0.1:9001 并完成令牌握手
   │
   ├─> 6. 渲染器发送 ready 事件 → InstanceSession 下发启动指令序列
   │      （startup salvo，共 7 条指令 + set_hit_areas）
   │
   └─> 7. model_loaded 事件 → 更新实例状态 → Scheduler 启动
          （QTimer 闲时动作节奏）→ 正常运行
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
    │       └─ InstanceManager 建立侧边栏花名册条目（QAbstractListModel）
    │
    └─ 每个实例由独立 InstanceSession 管理：
        ├─ 独立渲染器子进程（ProcessManager）
        ├─ 独立 WebSocket 连接（WsServer 按 instanceId 路由）
        ├─ 独立 Scheduler（闲时动作，QTimer）
        └─ 独立语音包挂载（MountedBehaviorEngine）

> **渲染后端选择**：OpenGL 与 Vulkan 为编译期切换（`-DUSE_VULKAN=ON`，无运行时切换），`build.py` 同时产出 OpenGL 与 Vulkan 两个渲染器变体。

配置文件结构：
  ~/.config/desktop-pet/
  ├── panel.json                  ← 面板配置
  ├── instances/{uuid-1}.json     ← 实例 1 配置
  ├── instances/{uuid-2}.json     ← 实例 2 配置
  └── mount.json                  ← 语音包挂载关系
```

---

## 四、关闭流程

控制面板遵循关闭动作策略（直接关闭 / 最小化到托盘 / 确认后关闭）：

```plain
控制面板退出
   │
   ├─> 1. 停止各实例 Scheduler — 停止闲时动作触发
   ├─> 2. 持久化面板与实例配置（QSaveFile 原子写入）
   ├─> 3. 向渲染器发送 shutdown 指令（等待 response）
   ├─> 4. 停止渲染器子进程（ProcessManager）
   └─> 5. 关闭 WsServer — 关闭 WebSocket 服务
```
