# 控制面板设计 (Java)

> **实现状态**：Phase 2（控制面板）已基本完成，包括 UI、业务逻辑、进程管理、系统托盘、崩溃恢复等核心功能。Phase 3（音频管理）已完成架构预留。
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
| 打包 | jlink + jpackage（内嵌最小化 JRE） |

---

## 二、模块概览

### 2.1 UI 层（JavaFX）

使用 JavaFX 21 构建用户界面，采用 FXML + CSS 分离布局与样式：

- **主窗口（MainWindowController）**：宠物当前状态显示、快捷操作按钮。`App.java` 继承 `javafx.application.Application`，作为 JavaFX 生命周期入口，在 `start()` 中创建 `AppOrchestrator`
- **设置面板（SettingsPanelController）**：模型选择、行为配置、启动项设置、拖拽模式切换（ToggleGroup）、闲时动作间隔（Spinner）。使用 FXML 布局，从 `MainWindowController` 打开独立 Stage
- **系统托盘（TrayManager）**：使用 `java.awt.SystemTray` API（JavaFX 未提供原生托盘支持）。关闭窗口时隐藏到托盘而非退出（`Platform.setImplicitExit(false)`），双击托盘图标切换窗口可见性，右键菜单包含 显示/隐藏、设置、退出
- **宠物管理**：模型切换（FileChooser 选择模型目录）、模型导入
- **音频管理（Phase 3）**：音频文件导入/删除、音频映射配置、音量控制。已完成 `AudioMapping` 记录定义

**UI 架构要点**：

| 关注点 | 方案 |
|:---|:---|
| 布局定义 | FXML 文件（`src/main/resources/fxml/`），通过 `FXMLLoader` 加载 |
| 样式管理 | CSS 样式表（`src/main/resources/css/`），支持运行时主题切换 |
| 事件绑定 | FXML Controller 类，使用 `@FXML` 注解绑定 UI 组件 |
| 线程安全 | UI 更新必须通过 `Platform.runLater()` 回到 JavaFX Application Thread；AWT 操作（托盘）通过 `SwingUtilities.invokeLater()` |
| 可视化设计 | 推荐使用 JavaFX Scene Builder 辅助设计 FXML 布局 |
| 模块系统 | `module-info.java` 声明 JPMS 模块，`opens` 包给 `javafx.fxml` 和 `com.google.gson` |

### 2.2 业务逻辑层

- **生命周期编排器（AppOrchestrator）**：**控制面板的核心中枢**，负责启动/关闭的完整编排：配置加载 → WebSocket Server 启动 → 事件处理器注册 → 渲染器进程启动 → 握手 → 模型加载 → 位置恢复。同时负责崩溃恢复（指数退避重启）和断连处理（关键指令缓存）。详见 [启动流程](../system/startup.md)
- **状态管理器（PetStateManager）**：维护宠物运行时状态。使用 `ReentrantReadWriteLock` 保证线程安全，`getState()` 返回不可变的 `PetState` 快照（Java Record）。当前不实现心情/活力等养成属性，架构预留权重打分扩展
- **交互处理器（InteractionHandler）**：接收渲染器上报的 `hit` 事件，查询模型行为映射配置，决定后续响应（构建 `play_motion` 指令并通过 `Protocol.createCommand()` 序列化发送）。内置默认映射（`head→TapHead`、`body→TapBody`），支持大小写容错（渲染器发送小写 key，配置使用 PascalCase）
- **定时任务（Scheduler）**：使用 `java.util.concurrent.ScheduledExecutorService` 实现定时闲时动作触发。支持 `pause()`/`resume()`（断连时暂停、重连后恢复）、`setIdleMotions()` 动态更新动作池、`updateInterval()` 修改触发间隔
- **配置管理器（ConfigManager）**：基于 Gson 读写 JSON 配置文件（`~/.config/desktop-pet/config.json`）。首次运行自动创建默认配置，JSON 损坏时降级为默认值（不崩溃），部分字段缺失时从 `PetConfig.defaults()` 合并。JSON 使用 `snake_case` 键名（`position_x`、`current_model_name`），Java Record 使用 `camelCase`
- **模型信息解析器（ModelInfoParser）**：直接解析 Cubism 的 `.model3.json` 文件，提取动作组（Map<String, Integer>）、表情列表、HitArea 列表。**重要**：渲染器的 `model_loaded` 事件中 motions/expressions 始终为空数组，Java 端必须自行解析模型文件获取这些信息
- **音频映射管理器（AudioMappingManager）**（Phase 3 架构预留）：已定义 `AudioMapping` 记录（`motionGroup` + `audioPath`），完整实现待 Phase 3

### 2.3 通信层（Network Layer）

基于 Java-WebSocket 库实现 WebSocket 服务端（控制面板为常驻进程，承担 Server 角色）：

- **PetWebSocketServer**：封装服务端启动、单连接管理（`activeConnection` 跟踪，新连接自动替换旧连接）、消息收发。使用独立线程处理网络 I/O，通过 `setMessageCallback` / `setConnectionCallback` 回调转发消息和连接状态变化
- **MessageDispatcher**：按消息 `type` 分路由——`response` 类型通过 `CompletableFuture` 匹配 `id` 完成回执（`expectResponse()` + `orTimeout()`），`event` 类型按 `action` 路由到注册的 `Consumer<Envelope>` 处理器。未注册 action 记录 WARN 日志，处理器异常隔离不传播
- **Protocol**：封装 Envelope 格式的序列化/反序列化（Gson）。**关键约定**：response 的 `success`/`error_code`/`error_message` 字段位于 JSON **顶层**（与 C++ 端保持一致），不在 `payload` 内。提供 `createCommand()`/`createEvent()`/`createResponse()` 工厂方法和 UUID 生成
- **连接管理**：监测渲染引擎连接状态，断开时通知 `AppOrchestrator`。详见 [容错与错误处理](../system/fault-tolerance.md)

### 2.4 进程管理器（ProcessManager）

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
