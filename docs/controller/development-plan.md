# 控制面板分步开发方案

> **文档定位**：本文档是**技术栈无关**的控制面板开发路线图，将完整开发工作切分为 10 个阶段。每个阶段产出一个**可验证的里程碑**，可独立运行和测试。
>
> **适用技术栈**：Qt 6 + QML（C++/PySide6）和 Slint（Rust）。每个阶段给出两种技术栈的实现要点。
>
> **前置文档**：
> - [架构蓝图](./architecture-blueprint.md) — 控制面板的功能规格与接口契约
> - [协议接口规格](../protocol/interface.md) — 25 条命令 + 13 个事件的完整定义
> - [技术栈调研报告](../research/control-panel-tech-stack.md) — Qt/Slint 选型依据

---

## 一、设计原则

| 原则 | 说明 |
|:---|:---|
| **里程碑驱动** | 每阶段结束有一个可运行的程序，而非"完成了某模块"的抽象说法 |
| **风险前置** | 最不确定的部分（WS 通信、子进程管理、框架适配）在 Phase 0-2 验证 |
| **技术栈无关** | 方案描述"做什么"和"接口契约"，不绑定特定 API；每阶段附 Qt/Slint 实现建议 |
| **增量式** | 每阶段在前一阶段基础上增加功能，不推翻重来 |
| **可并行** | 部分 UI 阶段（Phase 4-5）可与后台阶段（Phase 6-7）并行 |

---

## 二、阶段总览

### 2.1 依赖关系图

```
Phase 0 (PoC)
    │
    ▼
Phase 1 (通信层) ──────┐
    │                  │
    ▼                  ▼
Phase 2 (进程编排)   Phase 3 (配置层)
    │                  │
    └──────┬───────────┘
           │
           ▼
    Phase 4 (基础 UI)
           │
           ▼
    Phase 5 (实例管理 UI) ◄──── Phase 6 (运行时行为) [可并行]
           │
           ▼
    Phase 7 (系统集成)
           │
           ▼
    Phase 8 (高级功能)
           │
           ▼
    Phase 9 (部署打磨)
```

### 2.2 阶段速查表

| Phase | 名称 | 目标里程碑 | 工作量（人天） | 前置 |
|:---:|:---|:---|:---:|:---:|
| 0 | PoC 验证 | 最小通信闭环：启动渲染器 → WS 连接 → ready → load_model | 1–2 | 环境已装 |
| 1 | 通信层 | 完整 WS Server + Envelope + 消息路由 + Token 认证 | 3–4 | 0 |
| 2 | 进程编排 | 单实例完整生命周期（启动→握手→稳态→优雅关闭） | 2–3 | 1 |
| 3 | 配置持久化 | 多文件 JSON 配置（panel.json + instances/*.json） | 2–3 | 0 |
| 4 | 基础 UI | 无边框主窗口 + 主题系统 + 页面切换 + 侧边栏 | 3–5 | 2, 3 |
| 5 | 实例管理 UI | 完整实例管理界面（创建/删除/切换/参数调节） | 5–7 | 4 |
| 6 | 运行时行为 | 闲时调度 + 点击交互 + 拖拽持久化 + 模型发现 | 3–4 | 5 |
| 7 | 系统集成 | 系统托盘 + 开机自启 + 崩溃恢复 + 多实例 | 3–5 | 6 |
| 8 | 高级功能 | 资源监控 + 语音包 + 通知流（原字幕，已改为控制器侧气泡） + 布局 | 5–8 | 7 |
| 9 | 部署打磨 | 打包分发 + 性能优化 + 多平台验证 | 3–5 | 8 |
| | | **合计** | **30–46** | |

> 工作量估算基于一名有目标框架基础经验的开发者全职投入。含调试、测试、文档时间。

---

## 三、各阶段详细方案

### Phase 0: PoC 验证（1–2 天）

**目标**：用最小代码验证"新框架能与现有 C++ 渲染器通信"。这是**最高价值的前置投资**——如果通信链路不通，后续全部白费。

**里程碑**：一个控制台程序（无 UI 或最简 UI），执行后：
1. 启动 WebSocket Server（`127.0.0.1:9001`）
2. 启动渲染器子进程（`renderer.exe --port 9001 --instance-id 0 --token <hex> --model Hiyori`）
3. 收到渲染器的 `ready` 事件
4. 发送 `load_model` 命令
5. 收到 `model_loaded` 事件
6. 控制台输出 "PoC SUCCESS"
7. 发送 `shutdown` → 渲染器退出 → 程序结束

**任务清单**：

| # | 任务 | 说明 |
|:---:|:---|:---|
| 0.1 | 项目脚手架 | 创建项目结构（CMake/Cargo）、依赖声明、构建验证 |
| 0.2 | Token 生成 | 生成 32 字节随机数的 hex 字符串（64 字符） |
| 0.3 | WS Server 启动 | 绑定 127.0.0.1:9001，接受连接，解析查询参数 |
| 0.4 | 子进程启动 | 用 ProcessBuilder/QProcess/Command 启动渲染器，传 CLI 参数 |
| 0.5 | 消息收发 | 接收 ready → 发送 load_model → 接收 model_loaded |
| 0.6 | 优雅关闭 | 发送 shutdown → 等待进程退出 → 关闭 WS Server |

**验收标准**：
- [ ] 渲染器窗口出现并显示 Hiyori 模型
- [ ] 控制台输出 ready / model_loaded 事件日志
- [ ] shutdown 后渲染器进程干净退出（exit code 0）
- [ ] 无端口泄漏、无进程残留

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| WS Server | `QWebSocketServer(NonSecureMode)` + `listen(LocalHost, 9001)` | `tokio_tungstenite::accept_async` + `tokio::net::TcpListener` |
| JSON | `QJsonDocument` / `QJsonObject` | `serde_json::Value` |
| 子进程 | `QProcess::start(exe, args)` | `tokio::process::Command::new(exe).spawn()` |
| Token | `QRandomGenerator::bounded` 或 `std::random_device` | `rand::rngs::OsRng` |

**风险点**：
- WS 库的查询参数解析（instance_id、token）需要自行提取
- Origin 守卫（拒绝 http:// 开头的连接）需验证
- 渲染器工作目录必须设为 exe 所在目录（确保 Resources/ 可达）

---

### Phase 1: 通信层（3–4 天）

**目标**：实现完整的 WebSocket 通信层——Envelope 序列化、消息路由、Token 认证、所有命令/事件的类型化封装。

**里程碑**：通信层库可独立测试——注入 mock 消息验证路由，启动渲染器验证全协议闭环。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 1.1 | Envelope 数据模型 | type/action/id/payload/timestamp + response 的 success/error_code/error_message |
| 1.2 | 序列化/反序列化 | snake_case 键名；response 字段仅 type=="response" 时写入；缺字段静默丢弃 |
| 1.3 | 消息路由器 | response→按 id 匹配 pending request；event→按 action 分发到 handler |
| 1.4 | Token 认证 | `registerToken(id, token)` / `removeToken(id)`；onOpen 时三道闸门验证 |
| 1.5 | Origin 守卫 | 拒绝 Origin 以 `http://` 或 `https://` 开头的连接（关闭码 4001） |
| 1.6 | 连接管理 | 单实例单连接；新连接替换旧连接（Close 1000） |
| 1.7 | 命令工厂 | `createCommand(action, payload)` / `createEvent(action, payload)` 封装 |
| 1.8 | pending request 管理 | `expectResponse(id, timeout)` → CompletableFuture/Promise，10s 超时 |
| 1.9 | 事件处理器注册 | `registerEventHandler(action, handler)` — 按 action 注册回调 |

**验收标准**：
- [ ] 完整握手流程：Token 注册 → 渲染器连接 → 三道闸门 → ready 事件
- [ ] 发送任意命令（如 set_opacity）后渲染器有实际效果
- [ ] 收到 hit 事件能被正确路由到已注册的 handler
- [ ] 未注册 token 的连接被关闭（4002）
- [ ] 浏览器连接被拒绝（4001）
- [ ] Response 的 success/error_code/error_message 在 JSON 顶层（不在 payload 内）

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| 消息路由 | `QMap<QString, Handler>` + 信号槽 | `HashMap<String, fn(&Envelope)>` + tokio channels |
| 异步等待 | `QFuture` / 信号槽（非阻塞） | `tokio::sync::oneshot` / `oneshot::Receiver` |
| 线程模型 | WS 回调在 WS 线程 → 需 `QMetaObject::invokeMethod` 切到主线程 | tokio task → channel → UI 线程 |
| JSON Payload | `QJsonObject`（手动构建） | `serde_json::json!()` 宏 |

**关键注意事项**：
- `stats_state`、`layout_state` 是**伪响应事件**——按 action 路由，**不按 id 匹配**
- 消息回调在 WS I/O 线程上执行，修改 UI 状态必须切线程
- `payload` 始终为 JSON 对象（`{}`），绝不为 null

---

### Phase 2: 进程编排（2–3 天）

**目标**：实现渲染器子进程的完整生命周期管理——从启动到优雅关闭，包括 stdout 捕获和退出检测。

**里程碑**：单实例从启动到关闭的完整闭环，无需 UI 干预。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 2.1 | ProcessManager 模块 | 封装子进程启动、stdout/stderr 捕获、退出监听、强制终止 |
| 2.2 | 渲染器路径解析 | 根据 graphicsBackend 选择 `desktop-pet-renderer` 或 `desktop-pet-renderer-vulkan`（Windows 加 .exe） |
| 2.3 | CLI 参数构造 | `--port --instance-id --token --model --x --y --width --height` |
| 2.4 | 工作目录设置 | 设为渲染器 exe 所在目录（确保 `Resources/` 可达） |
| 2.5 | stdout 日志泵 | 独立线程读取 stdout → 转发到日志系统（不阻塞子进程输出缓冲区） |
| 2.6 | 退出回调 | 进程退出时通知上层（区分正常退出与崩溃） |
| 2.7 | 优雅关闭三阶段 | shutdown 指令 → 5s 等待 → destroyForcibly/kill -9 |
| 2.8 | 启动齐射封装 | 收到 ready 后自动发送 7 条初始化命令（load_model 到 set_layout；原 9 条中的两条字幕指令已随字幕系统移除而取消） |

**验收标准**：
- [ ] 渲染器启动后自动加载配置的模型
- [ ] stdout 输出实时出现在控制面板日志中
- [ ] shutdown 后渲染器在 5s 内退出
- [ ] 超时后强制终止（渲染器进程从任务管理器消失）
- [ ] 进程退出后 WS 连接自动关闭
- [ ] 无端口泄漏（重启后 9001 可重新绑定）

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| 进程启动 | `QProcess::setWorkingDirectory` + `start` | `Command::current_dir` + `spawn` |
| stdout 捕获 | `readyReadStandardOutput` 信号 | `child.stdout.take().unwrap()` + `BufReader` |
| 退出监听 | `finished(exitCode, status)` 信号 | `child.wait().await` |
| 强制终止 | `kill()` (SIGKILL/TerminateProcess) | `child.kill()` |
| 线程安全 | `QProcess` 信号在主线程 | tokio task 自动异步 |

---

### Phase 3: 配置持久化层（2–3 天）

**目标**：实现多文件 JSON 配置管理——加载、保存、默认值合并、损坏恢复。

**里程碑**：配置层可独立测试——给定任意 JSON 文件（含损坏），能正确加载或降级。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 3.1 | 配置目录管理 | `~/.config/desktop-pet/`（Linux）/ `%USERPROFILE%\.config\desktop-pet\`（Windows） |
| 3.2 | InstanceConfig 数据模型 | 24 字段（id, label, rendererPath, graphicsBackend, modelName, dialoguePack, ...；原 7 个字幕字段已移除） |
| 3.3 | PanelConfig 数据模型 | 11 字段（窗口位置/主题/字号/实例列表/启动退出行为） |
| 3.4 | InstanceConfigManager | save/load/delete/loadAll，每实例一个 `instances/<uuid>.json` |
| 3.5 | PanelStateManager | `panel.json` 读写 + 旧格式 `panel-state.json` 迁移 |
| 3.6 | JSON 健壮性 | 损坏→降级默认值；字段缺失→从 defaults 合并；首次运行→创建默认 |
| 3.7 | 原子写入 | 写临时文件 → 重命名（避免半写损坏） |
| 3.8 | snake_case 映射 | JSON 键名 snake_case ↔ 内部数据模型 camelCase |

**验收标准**：
- [ ] 首次运行自动创建配置目录和默认文件
- [ ] 保存配置后重启能恢复
- [ ] 手动破坏 JSON（删一半）不崩溃，降级默认值
- [ ] instanceIds 列表与 instances/*.json 文件一致
- [ ] 配置写入是原子的（测试中断不产生半写文件）

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| JSON 库 | `QJsonDocument` / `QJsonObject` | `serde_json` + `#[derive(Serialize, Deserialize)]` |
| 路径处理 | `QStandardPaths` 或 `QDir::home()` | `dirs::config_dir()` |
| 原子写 | 写 `.tmp` → `QFile::rename` | `tempfile::NamedTempFile` → `persist` |
| 默认值合并 | 手动 `mergeWithDefaults()` | `#[serde(default)]` 字段级默认值 |

---

### Phase 4: 基础 UI 框架（3–5 天）

**目标**：搭建控制面板的 UI 骨架——无边框主窗口、主题系统、页面切换器、侧边栏。

**里程碑**：一个可运行的控制面板窗口，有标题栏、主题切换、侧边栏（空）、页面切换（欢迎页/占位页）。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 4.1 | 无边框窗口 | 自定义标题栏（拖拽移动、最小化、关闭按钮）+ 8 方向边缘拉伸 |
| 4.2 | 页面切换器 | StackPane/可见性切换：欢迎页 / 实例详情 / 设置 / 监控 |
| 4.3 | 标题栏控件 | 主题下拉框、设置按钮、监控按钮、最小化、关闭 |
| 4.4 | 侧边栏框架 | 实例列表容器（此阶段为空壳）、"添加实例"按钮 |
| 4.5 | 主题系统 | 基础样式 + 主题覆盖；至少实现 3 套主题；运行时切换 + 持久化 |
| 4.6 | 欢迎页 | 无实例时显示：图标/标题/CTA 按钮 + 环境检测面板（可选） |
| 4.7 | 窗口状态恢复 | 从 PanelConfig 恢复窗口位置/尺寸/主题 |

**验收标准**：
- [ ] 标题栏可拖拽移动窗口
- [ ] 8 方向边缘可拉伸窗口
- [ ] 主题切换实时生效（至少 3 套）
- [ ] 关闭按钮的行为可配（退出/最小化到托盘——此阶段先退出）
- [ ] 窗口位置/尺寸重启后恢复
- [ ] 无实例时显示欢迎页

**技术栈实现要点**：

| 维度 | Qt 6 (QML) | Slint (Rust) |
|:---|:---|:---|
| 无边框窗口 | `flags: Qt.FramelessWindowHint` + `color: "transparent"` | `Window { background: transparent; }` + winit 配置 |
| 标题栏拖拽 | `DragHandler` 或 `MouseArea` + `startSystemMove` | `Window::on-mouse-event` 手动处理 |
| 主题系统 | QML `QtObject` + 动态 stylesheet 加载 | Slint 全局 `global Theme { ... }` + 条件样式 |
| 页面切换 | `StackView` 或可见性切换 | `if page == "detail" : InstanceDetail { ... }` |
| 边缘拉伸 | 自定义 MouseArea + `startSystemResize` | 手动实现（winit `set_cursor` + drag） |

> ⚠️ Slint 的无边框窗口 + 8 方向拉伸需要手动实现较多底层逻辑。Qt 的 QML 在这方面有成熟的内置支持（`startSystemMove`/`startSystemResize`）。如果 Slint 实现成本过高，可先只做标题栏拖拽，边缘拉伸推迟。

---

### Phase 5: 实例管理 UI（5–7 天）

**目标**：完整的实例管理界面——创建/删除/切换实例、模型选择、参数调节面板、实时生效。

**里程碑**：用户可通过 UI 创建宠物实例、切换模型、调节透明度/帧率/音量等参数，所有变更实时同步到渲染器。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 5.1 | 实例数据模型 | 可观察的实例属性（label, model, status, connected, opacity, ...） |
| 5.2 | 侧边栏实例列表 | 动态构建实例卡片（标签/模型/状态/连接徽章） |
| 5.3 | 实例创建流程 | 对话框输入标签 → 生成 UUID → 写配置 → 加入列表 → 启动 |
| 5.4 | 实例删除流程 | 确认对话框（默认聚焦取消）→ 停止渲染器 → 删配置 → 移除 |
| 5.5 | 模型选择 | 扫描 `Resources/Models/` → ComboBox → `load_model` 命令 |
| 5.6 | 动作/表情触发 | 解析 .model3.json → 构建动作网格 + 表情按钮 → play_motion/set_expression |
| 5.7 | 参数调节面板 | 透明度滑块/拖拽模式/帧率/音量+静音/位置/自启动开关 |
| 5.8 | 参数实时同步 | 滑块变更 → 立即发送对应命令（set_opacity/set_fps/set_volume...） |
| 5.9 | 配置持久化联动 | 参数变更 → 更新 InstanceConfig → 异步保存到 JSON |
| 5.10 | 日志面板 | 滚动列表（最多 50 条）+ 清空按钮；命令/事件日志实时追加 |

**验收标准**：
- [ ] 创建实例后渲染器自动启动并加载模型
- [ ] 切换模型后渲染器画面更新
- [ ] 透明度滑块实时改变渲染器窗口透明度
- [ ] 帧率切换实时生效（自适应↔固定）
- [ ] 音量滑块 + 静音开关实时生效
- [ ] 删除实例有二次确认
- [ ] 日志面板实时显示命令发送和事件接收
- [ ] 参数变更持久化（重启后恢复）

**技术栈实现要点**：

| 维度 | Qt 6 (QML) | Slint (Rust) |
|:---|:---|:---|
| 可观察模型 | `QAbstractListModel` 或 `Q_PROPERTY` | `slint::VecModel` + `SharedString` |
| 数据绑定 | QML 属性绑定（自动） | Slint 属性绑定（`property <...>`) |
| 对话框 | `QtQuick.Dialogs` 或自定义 Popup | Slint `Dialog` 或条件渲染 Rectangle |
| 滑块 | `Slider { from: 0.1; to: 1.0 }` | `Slider { minimum: 0.1; maximum: 1.0 }` |
| 列表视图 | `ListView { model: ...; delegate: ... }` | `for item[i] in model : Rectangle { ... }` |

> **模型能力发现**（5.6）是此阶段的技术难点：需要解析 `.model3.json`（JSON 格式）提取 `FileReferences.Motions`（组名→数量）和 `FileReferences.Expressions[*].Name`。渲染器的 `model_loaded` 事件中 motions/expressions 始终为空数组，必须自行解析。

---

### Phase 6: 运行时行为（3–4 天）

**目标**：实现闲时动作调度、点击交互响应、拖拽位置持久化——让宠物"活起来"。

**里程碑**：宠物会自动做闲时动作、点击有反应、拖拽后位置被记住。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 6.1 | 闲时调度器 | 定时器 + 随机选择 idle 动作 → play_motion；支持 pause/resume/triggerNow/updateInterval |
| 6.2 | 点击交互处理 | 收到 hit 事件 → 查映射（默认 head→TapHead, body→TapBody）→ play_motion |
| 6.3 | 交互映射大小写容错 | 渲染器发小写 area_id（"head"），配置可能用 PascalCase（"Head"） |
| 6.4 | 拖拽位置持久化 | 收到 drag_end → 提取 window_x/window_y → 保存到 InstanceConfig |
| 6.5 | 动作完成联动 | 收到 motion_finished 后，若非 idle 动作 → 触发 scheduler.triggerNow() |
| 6.6 | 模型能力发现 | 解析 .model3.json 提取 motionGroups/expressions/hitAreas |
| 6.7 | HitArea 缓存 | 缓存到 `hit_area_cache.json`，避免重复解析 |
| 6.8 | 状态同步齐射 | 收到 ready 后发送 set_hit_areas（model_loaded 后） |

**验收标准**：
- [ ] 宠物空闲时自动播放闲时动作
- [ ] 点击宠物头部/身体触发对应动作
- [ ] 拖拽宠物后关闭重开，位置恢复
- [ ] 闲时间隔可调（1–60 秒），运行时修改立即生效
- [ ] 断连时调度器暂停，重连后恢复

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| 定时器 | `QTimer::singleShot` / `start(interval)` | `tokio::time::interval` 或 slint::Timer |
| 随机选择 | `QRandomGenerator::bounded` | `rand::seq::SliceRandom::choose` |
| .model3.json 解析 | `QJsonDocument` 手动提取 | `serde_json::Value` + 点路径 |

---

### Phase 7: 系统集成（3–5 天）

**目标**：系统托盘、开机自启、崩溃恢复、多实例并行——让控制面板成为合格的桌面常驻应用。

**里程碑**：关闭到托盘、开机自启、崩溃自动恢复、同时养多只宠物。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 7.1 | 系统托盘 | 图标 + 右键菜单（显示/隐藏/设置/退出）+ 双击切换可见性 |
| 7.2 | 关闭到托盘 | 关闭按钮→隐藏窗口（不退出进程）；需禁用框架的"关闭即退出" |
| 7.3 | 开机自启 | Windows: 注册表 `HKCU\...\Run`；Linux: `~/.config/autostart/desktop-pet.desktop` |
| 7.4 | 崩溃恢复 | 渲染器非预期退出→指数退避 [2s,4s,8s,16s,30s]→最多 5 次 |
| 7.5 | manuallyStopping 标志 | 区分"用户主动停止"与"崩溃"；主动停止不触发重启 |
| 7.6 | 多实例并行 | 每实例独立 ProcessManager/Dispatcher/Scheduler，独立 WS 连接 |
| 7.7 | 实例独立端口/token | 共享 WS Server（9001）但每实例不同 instance_id + token |
| 7.8 | 关闭行为配置 | PanelConfig.closeAction: "exit" / "hide_to_tray"；confirmOnExit 开关 |

**验收标准**：
- [ ] 系统托盘图标显示，双击切换窗口
- [ ] 关闭按钮最小化到托盘（当配置为 hide_to_tray）
- [ ] 开机后控制面板自动启动
- [ ] 手动 kill 渲染器进程，控制面板自动重启它
- [ ] 连续 kill 5 次后停止重启，标记错误
- [ ] 同时运行 2+ 实例互不干扰
- [ ] 正常退出时所有子进程干净终止

**技术栈实现要点**：

| 维度 | Qt 6 (C++) | Slint (Rust) |
|:---|:---|:---|
| 系统托盘 | `QSystemTrayIcon`（Widgets 模块，黄金标准） | `tray-icon` crate 或 Slint 1.17 `SystemTrayIcon` |
| 禁用关闭即退出 | `QGuiApplication::setQuitOnLastWindowClosed(false)` | winit `with_window` 配置 |
| 注册表 | `QSettings` 或 `reg.exe` via QProcess | `winreg` crate |
| .desktop 文件 | `QFile::write` | `std::fs::write` |

---

### Phase 8: 高级功能（5–8 天）

**目标**：资源监控、语音包挂载、通知流（气泡信息流）、布局管理——将控制面板从"能用"提升到"好用"。

> **历史变更**：本阶段原设计为渲染器字幕系统（show_subtitle/set_subtitle_style 等），后改为 Qt 控制器侧通知流（气泡信息流）——渲染器字幕系统已整体移除，文案由控制器气泡直接显示。参见 [通知流设计](../system/notification-stream.md)。

**里程碑**：监控页显示 CPU/GPU 曲线、语音包可挂载触发扩展动作、对话文案以气泡形式显示。

**任务清单**：

| # | 任务 | 关键细节 | 子阶段 |
|:---:|:---|:---|:---:|
| 8.1 | 资源监控页 | 双列折线图（控制面板进程 + 渲染器进程）、2s 轮询、60 点环形缓冲 | 2 天 |
| 8.2 | 控制面板自采集 | 进程级 CPU%/RSS 采集（原生进程资源指标） | 含上 |
| 8.3 | 渲染器采集 | get_stats → stats_state 事件路由 → 更新图表 | 含上 |
| 8.4 | 语音包扫描 | 扫描 `Resources/VoicePacks/`，识别含 meta.mko 的目录 | 1 天 |
| 8.5 | meta.mko 解析 | Protobuf 解析 → VoicePackInfo（groups/modules/actions） | 1 天 |
| 8.6 | 挂载配置管理 | mount.json 读写；loadForModel/saveForModel | 含上 |
| 8.7 | 行为引擎 | hit 事件→优先走语音包 play_motion_ext，否则回退基础映射 | 1 天 |
| 8.8 | ~~字幕显示~~ → 通知流气泡 | ~~show_subtitle 命令 + 样式预设~~ 改为控制器侧气泡（NotificationStreamModel/Controller + BubbleStreamWindow） | 1 天 |
| 8.9 | ~~字幕调整模式~~ → 对话包 | ~~set_subtitle_adjust_mode + set_subtitle_layout~~ 改为 DialoguePackParser/Scanner/Scheduler（对话包发现、解析、调度） | 含上 |
| 8.10 | 布局管理 | set_layout/get_layout/reset_layout + layout_changed 持久化 | 1 天 |

**验收标准**：
- [ ] 监控页显示 6 条折线图（2 控制面板 + 4 渲染器），2s 更新
- [ ] GPU 不可用时显示"—"（Linux stub 场景）
- [ ] 数据陈旧（>10s 无更新）时显示警告
- [ ] 挂载语音包后点击宠物触发扩展动作（play_motion_ext）
- [ ] ~~字幕显示在渲染器窗口，样式可切换~~ → 对话文案以通知流气泡显示（屏幕右上角，自动消失）
- [ ] 布局参数（offset/scale）可调并持久化

**技术栈实现要点**：

| 维度 | Qt 6 (QML) | Slint (Rust) |
|:---|:---|:---|
| 图表 | `QtCharts` (QLineSeries) 或第三方 | 自绘（Slint `Path` + `Rectangle`）或 `plotters` crate |
| Protobuf | `protobuf` 库（C++） | `prost` crate（Rust 原生 Protobuf） |
| 系统采集 | 平台 API（Win: `GetProcessTimes`/`psapi`；Linux: `/proc/self/`）或 SIGAR | `sysinfo` crate（成熟） |

---

### Phase 9: 部署与打磨（3–5 天）

**目标**：打包分发、性能优化、多平台验证——产出可交付的最终产品。

**任务清单**：

| # | 任务 | 关键细节 |
|:---:|:---|:---|
| 9.1 | 部署打包 | Qt: windeployqt / Slint: 静态编译；生成可分发文件夹 |
| 9.2 | 安装包 | Windows: NSIS/Inno Setup；Linux: AppImage/.deb |
| 9.3 | 启动优化 | 冷启动 < 1s；延迟加载非关键模块 |
| 9.4 | 内存优化 | 目标：Qt 闲置 < 30MB / Slint 闲置 < 15MB |
| 9.5 | Windows 测试 | 10/11 验证：托盘、自启、透明窗口、多实例 |
| 9.6 | Linux 测试 | Ubuntu(X11)/Fedora(Wayland) 验证：同上 |
| 9.7 | 日志系统 | 文件轮转日志（按大小/日期）；SLF4J/log4rs/tracing 等价物 |
| 9.8 | 异常处理审计 | 确保所有 IO/网络/进程操作有 try-catch，不崩溃 |

**验收标准**：
- [ ] 分发包在干净系统上可运行（无开发依赖）
- [ ] 冷启动到 UI 可见 < 1s
- [ ] 闲置内存：Qt < 30MB / Slint < 15MB
- [ ] Windows + Linux 双平台功能一致
- [ ] 24h 稳定性测试无崩溃、无内存泄漏

---

## 四、两种技术栈的架构映射

### 4.1 模块映射总表

| 功能模块 | Qt 6 (C++/QML) | Slint (Rust) |
|:---|:---|:---|
| WebSocket Server | `QWebSocketServer` | `tokio-tungstenite` |
| JSON 序列化 | `QJsonDocument` / `QJsonObject` | `serde_json` + `serde` derive |
| 子进程管理 | `QProcess` | `tokio::process::Command` |
| 定时器 | `QTimer` | `slint::Timer` / `tokio::time` |
| 系统托盘 | `QSystemTrayIcon` | Slint 1.17 `SystemTrayIcon` / `tray-icon` crate |
| 配置文件 | `QJsonDocument` + `QFile` | `serde_json` + `std::fs` |
| 开机自启 | `QSettings` / `reg.exe` | `winreg` crate / `std::fs::write` |
| UI 布局 | QML 声明式 | Slint `.slint` DSL |
| 数据绑定 | QML 属性绑定（自动） | Slint 属性绑定 |
| 主题切换 | 动态 stylesheet 加载 | 全局 `Theme` struct + 条件样式 |
| 图表 | `QtCharts` | 自绘或 `plotters` |
| 系统采集 | 平台 API（Win: `GetProcessTimes`/`psapi`；Linux: `/proc/self/`）或 SIGAR | `sysinfo` crate |
| Protobuf | `protobuf` C++ | `prost` crate |
| 日志 | `spdlog` 或 Qt 自带 | `tracing` / `log` + `env_logger` |
| 构建 | CMake + Ninja | Cargo |
| 部署 | `windeployqt` | 静态编译（默认） |

### 4.2 线程模型对比

| 线程 | Qt 6 | Slint (Rust) |
|:---|:---|:---|
| UI 主线程 | Qt 事件循环（主线程） | Slint 窗口事件循环（主线程） |
| WS I/O | Qt WS 内部线程 → 信号槽切到主线程 | tokio runtime（独立线程池）→ channel 切到主线程 |
| 子进程 stdout | `QProcess` 信号（主线程） | tokio task（异步读取） |
| 调度器 | `QTimer`（主线程） | `slint::Timer`（主线程）或 tokio task |
| 资源采集 | `QTimer` 触发 → 同步采集 | tokio task → channel → UI |

> **关键差异**：Qt 的信号槽机制天然跨线程安全（`QueuedConnection`）；Slint 需要显式用 channel 或 `slint::invoke_from_event_loop` 将数据从 tokio 线程传递到 UI 线程。

### 4.3 项目结构建议

**Qt 6 (CMake + QML)**:
```
controller_qt/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── network/          # Phase 1-2
│   │   ├── WsServer.*
│   │   ├── MessageDispatcher.*
│   │   └── Protocol.*
│   ├── core/             # Phase 3, 6
│   │   ├── ConfigManager.*
│   │   ├── InstanceConfig.*
│   │   ├── Scheduler.*
│   │   └── InteractionHandler.*
│   ├── system/           # Phase 7
│   │   ├── TrayManager.*
│   │   └── AutoLaunch.*
│   └── ui/               # Phase 4-5, 8
│       ├── MainWindow.*
│       └── ...
├── qml/                  # QML 文件
│   ├── Main.qml
│   ├── Sidebar.qml
│   └── ...
└── resources/
```

**Slint (Cargo)**:
```
controller_slint/
├── Cargo.toml
├── build.rs              # Slint 编译脚本
├── src/
│   ├── main.rs
│   ├── network/          # Phase 1-2
│   │   ├── ws_server.rs
│   │   ├── dispatcher.rs
│   │   └── protocol.rs
│   ├── core/             # Phase 3, 6
│   │   ├── config.rs
│   │   ├── scheduler.rs
│   │   └── interaction.rs
│   ├── system/           # Phase 7
│   │   ├── tray.rs
│   │   └── autolaunch.rs
│   └── ui/               # Phase 4-5, 8
│       └── ...
├── ui/                   # .slint 文件
│   ├── main.slint
│   ├── sidebar.slint
│   └── ...
└── resources/
```

---

## 五、风险登记册

| # | 风险 | 阶段 | 概率 | 影响 | 缓解措施 |
|:---:|:---|:---:|:---:|:---:|:---|
| R1 | WS 库不支持查询参数解析 | 0-1 | 低 | 高 | Phase 0 PoC 先验证；自行解析 URL query string |
| R2 | Slint 无边框窗口 + 8 方向拉伸成本高 | 4 | 中 | 中 | 先只做标题栏拖拽；边缘拉伸推迟或用第三方 crate |
| R3 | Slint 图表控件缺失 | 8 | 高 | 低 | 自绘 Path + Rectangle 或用 plotters；或跳过监控页 |
| R4 | 透明窗口在 Linux Wayland 上异常 | 4 | 中 | 高 | 测试 GNOME/KDE Wayland；用逐像素 alpha（不依赖 setWindowOpacity） |
| R5 | 系统托盘在 GNOME 上不显示 | 7 | 中 | 中 | 需要 AppIndicator 扩展；文档说明安装方法 |
| R6 | 全局快捷键在 Wayland 无解 | 7+ | 高 | 低 | Wayland 上不提供全局快捷键；或用组合器特定 API |
| R7 | MinGW 版本冲突（Qt 13.1 vs 系统 15.2） | 0-9 | 低 | 高 | 用 Qt 自带 MinGW 13.1 编译 Qt 项目；系统 MinGW 只给渲染器用 |
| R8 | Rust GNU toolchain 与 Qt MinGW 混用 | 0-9 | 低 | 中 | 已解决：Rust 切到 GNU target；两者都可用 MinGW ld |

---

## 六、时间规划建议

### 6.1 单技术栈全职开发

| 阶段 | 天数 | 累计 |
|:---|:---:|:---:|
| Phase 0 | 1–2 | 2 |
| Phase 1 | 3–4 | 6 |
| Phase 2 | 2–3 | 9 |
| Phase 3 | 2–3 | 12 |
| Phase 4 | 3–5 | 17 |
| Phase 5 | 5–7 | 24 |
| Phase 6 | 3–4 | 28 |
| Phase 7 | 3–5 | 33 |
| Phase 8 | 5–8 | 41 |
| Phase 9 | 3–5 | 46 |
| **合计** | **30–46** | |

### 6.2 两种技术栈并行开发建议

**方案 A：串行（推荐）**
- 先用 Qt 完成 Phase 0-7（约 33 天），验证方案可行性
- 再用 Slint 重走 Phase 0-7（更快，约 20 天，因方案已验证）
- Phase 8-9 各自独立完成

**方案 B：Phase 0 后并行**
- Phase 0 两种框架各做一次 PoC（2-3 天）
- 确认两者都可行后，Phase 1-7 并行开发
- 通信层/配置层/进程管理可共享设计（甚至共享测试用例）

**方案 C：选一种先行**
- Phase 0-4 用 Qt 先行（验证 UI 框架）
- 如果 Qt 体验好，继续完成；如果想试 Slint，Phase 5 开始切换

> **推荐方案 A**：Qt 的成熟度更高（系统托盘黄金标准、QML 热重载、windeployqt），先用它跑通全流程。Slint 作为轻量级替代方案后续跟进，此时方案已验证，风险大幅降低。

---

## 七、测试策略

### 7.1 每阶段测试重点

| Phase | 测试类型 | 重点 |
|:---|:---|:---|
| 0 | 冒烟测试 | 通信闭环是否通 |
| 1 | 单元测试 | Envelope 序列化/反序列化、消息路由、Token 验证 |
| 2 | 集成测试 | 启动→握手→关闭全流程；stdout 捕获；超时终止 |
| 3 | 单元测试 | 配置读写、损坏恢复、默认值合并、原子写入 |
| 4 | 手动验收 | 窗口操作（拖拽/拉伸/主题切换） |
| 5 | 手动验收 | 实例 CRUD、模型切换、参数实时生效 |
| 6 | 手动验收 | 闲时动作触发、点击响应、拖拽持久化 |
| 7 | 手动验收 | 托盘、自启、崩溃恢复、多实例 |
| 8 | 手动验收 | 监控数据、语音包动作、通知流气泡显示 |
| 9 | 端到端 | 全平台干净系统安装、24h 稳定性 |

### 7.2 共享测试用例

两种技术栈可以共享**协议测试用例**——用 JSON 文件描述输入消息和期望输出，两种实现都跑同一套用例验证协议正确性。

```
tests/protocol/
├── envelope_serialize.json      # Envelope → JSON 序列化用例
├── envelope_deserialize.json    # JSON → Envelope 反序列化用例
├── handshake_flow.json          # 握手全流程用例（7 条启动齐射）
├── command_all_20.json          # 20 条命令的 payload 构造用例（原 command_all_25.json，字幕指令移除后更名）
└── event_all_13.json            # 13 个事件的 payload 解析用例
```

---

> **文档版本**：v1.0 · 基于架构蓝图和协议接口规格编写
> 如实际开发中发现阶段划分不合理（某阶段过大/过小），应及时调整本文档
