# 控制面板技术栈调研报告

> **调研日期**：2026-07-13
> **调研目标**：为桌面宠物项目评估下一代控制面板的底层技术栈，替代当前的 JavaFX 实现
> **调研方法**：6 组并行 librarian 深度调研（Qt/Flutter/Avalonia/Compose+Slint+GTK-rs/社区对比/性能基准），覆盖官方文档、GitHub 源码与 issue、生产案例、学术论文、社区评价
> **决策导向**：本文档是**选型决策参考**，不是实现规格。每个候选给出明确评分与推荐/排除结论

---

## 一、调研背景

### 1.1 当前架构

项目采用 **控制面板（JavaFX）+ 渲染器（C++）** 分离架构：
- 控制面板是 WebSocket **Server**（端口 9001），管理渲染器子进程、提供配置 UI、处理业务逻辑
- 渲染器是 WebSocket **Client**，由控制面板启动，负责 Live2D 渲染、透明窗口、点击检测
- 控制面板是**普通窗口**（Tab 式 UI），不是透明浮窗——透明浮窗是渲染器的职责

### 1.2 迁移动机

- JavaFX 闲置内存 80–120MB，对常驻后台应用偏重
- JavaFX 生态萎缩（Oracle 淡出，OpenJFX 社区维护）
- 现代化 UI 能力不足（毛玻璃/动画/主题系统较弱）
- 系统集成依赖第三方库（系统托盘、全局快捷键、开机自启）

### 1.3 关键前提

控制面板的**核心职责**是管理而非渲染——它需要：WebSocket Server、子进程管理、系统托盘、配置 UI、系统事件监听。透明浮窗是渲染器（C++ GLFW 窗口）的工作。因此评估时**透明窗口能力是加分项而非必需项**，控制面板自身只需要支持无边框现代窗口即可。

---

## 二、评估标准

### 2.1 硬约束（不满足即排除）

| # | 约束 | 说明 |
|:---:|:---|:---|
| H1 | **原生渲染** | 非 WebView / 非 Chromium 嵌入（排除 Electron/NW.js/Tauri） |
| H2 | **跨平台** | 至少 Windows + Linux（X11/Wayland），macOS 加分 |
| H3 | **系统级事件监听** | 可监听电源/睡眠/锁屏/全局快捷键/USB 等系统事件 |
| H4 | **现代化 UI** | 支持动画、阴影、模糊/毛玻璃、圆角、主题切换 |
| H5 | **UI 开发便携** | 声明式 UI 或可视化设计工具，热重载加分 |
| H6 | **常驻友好** | 资源占用合理（目标 ≤ JavaFX 的 80–120MB） |

### 2.2 评估维度（13 项）

| 维度 | 权重 | 评估要点 |
|:---|:---:|:---|
| 资源占用（空闲 RSS） | 高 | 常驻后台应用的关键指标 |
| 系统托盘 | 高 | 桌面宠物的标配交互入口 |
| WebSocket Server | 高 | 核心通信职责 |
| 子进程管理 | 高 | 管理渲染器进程的生命周期 |
| 系统级事件 | 中 | 电源/锁屏/快捷键/USB/网络变化 |
| 无边框窗口 | 中 | 控制面板自身的现代 UI |
| 透明窗口 | 低 | 加分项（控制面板自身不强需要） |
| 现代 UI 能力 | 中 | 毛玻璃/动画/主题 |
| UI 开发便携性 | 中 | 声明式/热重载/设计工具 |
| 跨平台成熟度 | 高 | Windows + Linux 生产可用 |
| 与 C++ 渲染器协同 | 中 | 同语言生态加分，IPC 独立也行 |
| 许可证 | 中 | 可商用、无 copyleft 污染 |
| 生态健康度 | 中 | 社区、维护、长期前景 |

---

## 三、候选技术栈

| 候选 | 语言 | 渲染后端 | 许可证 |
|:---|:---|:---|:---|
| Qt 6 + QML | C++ / Python(PySide6) | 原生（OpenGL/Vulkan/Metal/D3D） | LGPL v3 / 商用 |
| Avalonia UI | C# (.NET) | Skia | MIT |
| Slint | Rust / C++ | Skia / 软件 / wgpu | GPLv3 / 免费桌面版 / 商用 |
| Flutter Desktop | Dart | Impeller (Vulkan/Metal/D3D) | BSD-3 |
| Compose Multiplatform Desktop | Kotlin (JVM) | Skia (Skiko) | Apache 2.0 |
| GTK4 + gtk-rs | Rust | Cairo / OpenGL / Vulkan | LGPL |

---

## 四、综合对比矩阵

### 4.1 功能与集成能力

| 维度 | Qt 6 | Avalonia | Slint | Compose MP | Flutter | GTK4+gtk-rs |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| 系统托盘 | ✅✅ 黄金标准 | ✅ 内置 | 🟢 1.17新增 | ✅ 内置 Tray | ✅ 社区包 | 🟡 Linux only |
| WebSocket Server | ✅ 原生 | ✅ 多选项 | ✅ tokio | ✅ Ktor | ✅ shelf | ✅ tokio |
| 子进程管理 | ✅ QProcess | ✅ Process | ✅ std::process | ✅ ProcessBuilder | ✅ dart:io | ✅ gio::Subprocess |
| 系统级事件 | ⚠️ 部分 | ⚠️ Win原生 | 🟡 全手动 | ✅ JNA | ⚠️ 碎片化 | ✅ Linux强 |
| 无边框窗口 | ✅ 原生 | ✅ 强 | ✅ Skia | ✅ 原生 | ⚠️ Win OK | 🟡 Linux OK |
| 透明窗口(Linux) | ✅ 逐像素OK | 🟡 组合器依赖 | 🟡 wgpu bug | ✅ | 🔴 **blocker** | 🟡 Win脆弱 |
| 现代 UI | ✅ MultiEffect | ✅ Fluent/Acrylic | 🟡 控件少 | ✅ Material3 | ✅✅ 最强 | ✅ libadwaita |
| UI 便携性 | ✅ QML热重载 | ✅ XAML+MVVM | 🟡 DSL | ✅ Compose | ✅✅ 热重载 | 🟡 XML/Rust |
| 跨平台成熟度 | ✅✅ 35年 | ✅ 生产就绪 | 🟡 年轻 | ✅ GA | ✅ 稳定 | 🔴 Win不可行 |
| 与C++协同 | ✅✅ 同语言 | ✅ 独立 | ✅ 同系统级 | ✅ JVM独立 | ✅ 独立 | 🔴 DLL噩梦 |

### 4.2 资源占用（常驻后台关键指标）

| 框架 | 空闲内存 | 冷启动 | 包体积 | vs JavaFX 80-120MB |
|:---|:---|:---|:---|:---|
| **Slint（软件渲染）** | **10–30 MB** | <50ms | 2.8–6 MB | ✅ 轻 6–10 倍 |
| **Qt 6 Widgets** | **17–30 MB** | 100–300ms | 11–27 MB | ✅ 轻 4–6 倍 |
| **Qt 6 QML** | 40–80 MB | 200–400ms | ~58 MB | ✅ 轻 1.5–3 倍 |
| **Flutter Desktop** | 70–90 MB | ~19ms | 25–30 MB | ⚠️ 略轻 |
| ━━ **JavaFX（基线）** ━━ | **80–120 MB** | 1–2s | ~200MB(JRE) | ━━ 基线 ━━ |
| **Compose MP** | 108–146 MB | 1–2s | 50–150MB | ❌ 同级无改善 |
| **Avalonia (JIT)** | 60–150 MB | 0.5–1.5s | 15–150MB | ⚠️ 同级偏重 |
| **GTK4** | 50–200 MB | 0.5–2s | 5–20MB+依赖 | ❌ 同级偏重 |
| **Electron（参考）** | 100–180 MB | 1–3s | 150–200MB | ❌ 重 2 倍 |

### 4.3 许可证对比

| 框架 | 许可证 | 商用影响 |
|:---|:---|:---|
| **Avalonia** | MIT | ✅ 无任何限制，最宽松 |
| **Flutter** | BSD-3 | ✅ 几乎无限制 |
| **Compose MP** | Apache 2.0 | ✅ 无限制 |
| **Qt 6** | LGPL v3 / 商用 | ⚠️ 动态链接可商用，需提供 Qt 源码；静态链接需商用许可 |
| **GTK4** | LGPL | ⚠️ 同 Qt，动态链接可商用 |
| **Slint** | GPLv3 / 免费桌面版 / 商用 | ⚠️ 三选一，免费桌面版有限制（不可独立分发 Slint） |

---

## 五、各技术栈详细评估

### 5.1 Qt 6 + QML — ✅ 综合最优

**版本**：6.8 LTS（2024.10，商用支持至 2029）/ 6.11.x 最新

#### 核心能力

| 能力 | API | 成熟度证据 |
|:---|:---|:---|
| 系统托盘 | `QSystemTrayIcon` | OBS Studio、Bitcoin Core、qBittorrent 生产使用 |
| WebSocket Server | `QWebSocketServer` | Supercollider、Nextcloud、x64dbg 生产使用 |
| 子进程管理 | `QProcess` | 35 年 API 稳定，完整生命周期 |
| 无边框透明窗口 | `FramelessWindowHint` + `WA_TranslucentBackground` | KikoPlay、QMDemo 生产使用 |
| 现代 UI | `MultiEffect`(模糊/阴影/着色单 shader) + QML 动画 | — |
| 热重载 | `qmlscene` + Qt Creator Live Preview | — |

#### 资源占用

- 空闲内存：**17–30 MB**（Widgets）/ 13 MB（QML Hello World）
- 冷启动：100–300ms
- 包体积：静态编译 13MB / windeployqt 文件夹 49–58MB
- CPU 空闲：0–1%

#### 与 JavaFX 对比

比 JavaFX 轻 4–6 倍。QML 热重载优于 FXML+Controller 的重编译循环。MultiEffect 单 shader 管线优于 JavaFX 的 BoxBlur+DropShadow 链式效果。

#### 弱点

| 弱点 | 影响 | 缓解方案 |
|:---|:---|:---|
| 系统级事件无统一 API | 电源/锁屏/USB 需平台代码 | ~200 行 `#ifdef` 或 QHotkey 库（662★） |
| Wayland `setWindowOpacity` 不支持 | 整窗透明度不可调 | 对控制面板无影响（用逐像素 alpha） |
| Wayland 全局快捷键无解 | 纯 Wayland 无 `XGrabKey` 等价物 | 需组合器特定 API（KGlobalAccel 等） |
| LGPL 合规 | 需提供 Qt 源码或书面承诺 | 动态链接 + About 对话框声明即可 |

#### 语言选择

- **C++（推荐）**：与渲染器同语言，性能最佳，直接复用 C++ 经验
- **PySide6（备选）**：LGPLv3 官方绑定，快速原型；但二进制 ~100MB（Python+Qt），失去资源优势

---

### 5.2 Avalonia UI — ✅ 最佳现代化 UI

**版本**：11.x（最新 11.3.x），MIT 许可

#### 核心能力

| 能力 | 评估 |
|:---|:---|
| 系统托盘 | ✅ 内置 `TrayIcon` 控件，无需额外包 |
| WebSocket Server | ✅ `System.Net.WebSockets` / Fleck / ASP.NET Core |
| 子进程管理 | ✅ `System.Diagnostics.Process`，`Kill(entireProcessTree: true)` |
| 无边框窗口 | ✅ `WindowDecorations="None"` + `ExtendClientAreaToDecorationsHint` |
| 现代 UI | ✅ FluentTheme + ExperimentalAcrylicBorder + Mica（Win11 风格） |
| UI 开发 | ✅ XAML + MVVM + `dotnet watch` 热重载 + F12 诊断工具 |

#### 生产验证

- **Schneider Electric Harmony HMI**：嵌入式 Linux，512MB RAM，无窗口系统直接帧缓冲渲染
- **DiskDigger**：WinForms→Avalonia 移植，4 周完成，60MB 自包含 exe
- **sourcegit**：跨平台 Git 客户端，无边框窗口生产使用

#### 资源占用

- 空闲内存：60–150 MB（JIT）/ 30–60 MB（NativeAOT 估算，尚不成熟）
- 冷启动：500–1500ms（JIT 预热）
- 包体积：15–40MB（框架依赖）/ ~150MB（自包含含运行时）

#### 弱点

| 弱点 | 影响 |
|:---|:---|
| 资源占用偏高 | 60–150MB，与 JavaFX 同级或更重 |
| NativeAOT 不成熟 | Avalonia 11.x 反射重，trimming 易断裂 |
| 系统事件仅 Windows 原生 | Linux 需 DBus（Tmds.DBus），macOS 需 NSWorkspace |
| 点击穿透需 P/Invoke | ~80 行 Win32/X11 代码（官方文档引导） |
| 无原生 Wayland 支持 | 通过 XWayland 运行，无分数缩放/帧回调 |

---

### 5.3 Slint — ✅ 资源占用冠军，生态年轻

**版本**：1.17（2026.06，刚加入系统托盘）

#### 核心能力

| 能力 | 评估 |
|:---|:---|
| 资源占用 | ✅✅ **10–30MB（软件渲染）/ 30–50MB（GPU）** |
| WebSocket Server | ✅ tokio-tungstenite |
| 子进程管理 | ✅ std::process / tokio::process |
| 系统托盘 | 🟢 1.17 新增 `SystemTrayIcon`（2026.06 刚发布） |
| 现代 UI | 🟡 简洁但控件库稀疏 |

#### 资源占用（冠军级）

- 空闲内存：**2.6–13MB**（Rust 模板）/ **30MB**（rproc 进程监视器，软件渲染）
- 冷启动：<50ms
- 包体积：2.8–6MB 单一可执行文件，完全静态
- rproc 案例：从 egui 的 135MB 降到 Slint 的 30MB（4.5 倍优化）

#### 弱点（较多）

| 弱点 | 严重性 |
|:---|:---|
| 控件库稀疏 | 日期选择器、上下文菜单、拖放、富文本编辑都需自建 |
| 系统托盘刚加入 | 1.17（2026.06），仅 1 个月，预期有粗糙边缘 |
| 系统事件全手动 | Win32 crate / ashpd / D-Bus，无封装 |
| wgpu 透明窗口有 bug | [#9507](https://github.com/slint-ui/slint/issues/9507)，需用 Skia-OpenGL 后端 |
| 许可证复杂 | GPLv3 / 免费桌面版（不可独立分发 Slint）/ 商用三选一 |
| 生态年轻 | v1.0 于 2024，桌面集成功能（托盘/拖放/提示）1.17 才落地 |

---

### 5.4 Flutter Desktop — 🔴 排除（Linux 透明窗口 blocker）

#### 致命问题

**Linux 透明窗口是 6 年未解决的 blocker**：

| Issue | 日期 | 状态 |
|:---|:---|:---|
| [flutter#66751](https://github.com/flutter/flutter/issues/66751) | 2020.09 | **OPEN（6 年）** — GTK 渲染黑色背景 |
| [flutter#183558](https://github.com/flutter/flutter/issues/183558) | 2026.03 | **OPEN** — 无边框窗口都是新请求 |
| [flutter#184366](https://github.com/flutter/flutter/pull/184366) | 2026.03.30 | **OPEN 未合并** — 提议的修复，审查者仍在质疑 API 设计 |

#### 根本原因

Flutter 的 Linux 嵌入层基于 GTK（`FlView` 继承 `GtkGLArea`）。GTK + GL 渲染 + 组合器透明是多层问题：GTK 窗口需配置 RGBA visual，GLArea 需清除 alpha=0（引擎当前清除为不透明黑），组合器需支持 alpha。Windows 侧因 DWM 成熟 API 工作正常。

#### 其他评估

- 资源占用：70–90MB（中等）
- 现代 UI：✅✅ 最强（Impeller、BackdropFilter、Material 3）
- 系统事件：碎片化，需大量 platform channel 原生代码
- 社区评价：*"Flutter Desktop 是桌面优先新项目的错误选择"*（softaims.com 2026 诚实评测）

**结论**：Windows 可用但 Linux 透明窗口不生产就绪。对需要 Linux 支持的桌面宠物项目，Flutter 当前不可行。

---

### 5.5 Compose Multiplatform Desktop — 🟡 不推荐（资源无改善）

#### 评估

| 维度 | 评估 |
|:---|:---|
| 功能完整性 | ✅ GA 稳定，内置 Tray、Ktor WebSocket、ProcessBuilder |
| 资源占用 | ❌ **108–146MB（与 JavaFX 同级，JVM+Skia 开销）** |
| JVM 迁移 | ✅ 从 JavaFX 迁移概念一致（同为 JVM） |
| GraalVM AOT | ❌ 对 Compose 几乎不可用（AWT/Swing + 协程反射问题） |

#### 资源占用分析（JB#1632）

Hello World 内存分解（NMT）：
- 总提交：103.63 MB
- Java 堆：仅 8 MB
- **线程栈：50.29 MB**（21 线程 × ~2.4MB）← 最大开销
- Metaspace：15.36 MB
- JIT 代码：3.94 MB

"膨胀"来自 JVM 线程 + Skia 原生绑定，不是堆。与 JavaFX 同属 JVM+Skia 重量级。

#### 结论

从 JavaFX 迁移到 Compose MP **不会改善资源占用**（可能略好但不显著）。JetBrains Toolbox 案例从 Electron 迁移到 Compose 是值得的（200MiB→150MB），但从 JavaFX 迁移收益甚微。

---

### 5.6 GTK4 + gtk-rs — 🔴 排除（Windows 不可行）

#### 致命问题

**Windows 上的部署负担是 disqualifying 的**：

- 需 `gvsbuild build gtk4`（漫长构建）或 MSYS2 MinGW 路线
- 运行时分发需打包 **50+ DLL**（GTK、GLib、Pango、Cairo、HarfBuzz、gdk-pixbuf、librsvg、fontconfig、freetype 等）
- gvsbuild README 明确声明：*"AS IS, WITHOUT WARRANTY... 未测试... 即使安全问题也无法承诺及时更新"*
- MSIX/MSI 打包完全 DIY
- 透明覆盖窗口在 Win32 上无文档且脆弱

#### Linux 上的优势（但不够）

- 系统事件监听**最强**：GDBus、GSettings、GNetworkMonitor、GPowerProfileMonitor
- `gio::Subprocess` 提供最干净的异步子进程管理
- libadwaita 提供 GNOME 级别精致 UI
- 但 libadwaita 是 GNOME 专属，Windows 上渲染为无样式 GTK4

#### 资源占用

GTK4 相比 GTK3 **严重退化**：GTK3 空闲 5MB → GTK4 空闲 50–200MB（强制 GL/Vulkan 初始化 + shader 缓存）。

**结论**：项目以 Windows 为主平台（AGENTS.md：Windows/MinGW 当前），GTK4 不可行。仅 Linux-only 场景考虑。

---

## 六、性能基准深度对比

> 数据来源：szibele.com（Linux 同机对比）、cross-platform-test（Windows 同机对比）、各框架 GitHub issue、学术论文、rproc 案例研究。详见 [§十 参考来源](#十参考来源)。

### 6.1 空闲内存排名（常驻后台场景）

```
Slint(软件渲染)   10-30 MB    ◄── 冠军，比 JavaFX 轻 6-10 倍
Qt Widgets        17-30 MB    ◄── 比 JavaFX 轻 4-6 倍
Slint(GPU)        30-50 MB
Flutter Desktop   70-90 MB
Qt QML            40-80 MB
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
JavaFX(当前基线)  80-120 MB
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Compose MP        108-146 MB  ◄── 与 JavaFX 同级，无改善
Avalonia(JIT)     60-150 MB
GTK4              50-200 MB
Electron(参考)    100-180 MB  ◄── 已排除
```

### 6.2 关键发现

1. **JavaFX 基线是中游，不是轻量级**。80–120MB 与 Flutter、Compose MP、Avalonia 同级。JVM 线程栈（~50MB）和原生绑定是开销大头，堆仅 8–30MB。

2. **Rust/C++ 的 always-AOT 栈完胜**。Slint（10–30MB）和 Qt Widgets（17–30MB）比 JVM 系轻 4–10 倍，因为不携带 JIT、GC、线程栈开销、运行时元数据堆。

3. **GPU 渲染对 2D 闲置应用是 50–100MB 税**。rproc 案例的最大优化是**完全禁用 GPU 渲染器**（135MB→30MB）。适用于 Slint（软件渲染器）、GTK4（`GSK_RENDERER=cairo`）、Avalonia（`MaxGpuResourceSizeBytes=0`）。

4. **Avalonia 对常驻应用有风险**。多次确认的内存回归 bug（空窗口膨胀到 7.4GB [#16013](https://github.com/AvaloniaUI/Avalonia/issues/16013)，调整大小后 450MB [#3983](https://github.com/AvaloniaUI/Avalonia/issues/3983)）。NativeAOT 对 11.x 尚不实际。

5. **Compose MP 不会比 JavaFX 省内存**。同为 JVM + Skia。GraalVM Native Image 对 Compose 因 AWT/Swing + 协程反射问题几乎不可用。

6. **GTK4 不再轻量**。从 GTK3 的 5MB 退化到 50–200MB（强制 GL/Vulkan 初始化）。Windows 上体验二流。

---

## 七、推荐结论

### 7.1 推荐排序

| 排名 | 技术栈 | 适用场景 | 核心理由 |
|:---:|:---|:---|:---|
| 🥇 | **Qt 6 + QML (C++)** | 综合最优，默认选择 | 资源占用低(17-30MB)、系统集成黄金标准、与 C++ 渲染器同语言生态 |
| 🥈 | **Avalonia UI (.NET)** | 团队偏好 C# / 想要最现代 UI | MIT 许可、Fluent+Acrylic 开箱即用、生产验证(Schneider) |
| 🥉 | **Slint (Rust)** | 极致轻量是硬需求 | 10-30MB 冠军级资源占用，但生态年轻、控件少 |

### 7.2 排除项

| 技术栈 | 排除理由 | 严重性 |
|:---|:---|:---:|
| **Flutter Desktop** | Linux 透明窗口 6 年未解决（[#66751](https://github.com/flutter/flutter/issues/66751)），PR 2026.03 提交未合并 | 🔴 Blocker |
| **GTK4 + gtk-rs** | Windows 需打包 50+ DLL（无保修），透明窗口在 Win32 脆弱无文档 | 🔴 不可行 |
| **Compose MP** | 资源占用与 JavaFX 同级(108-146MB)，GraalVM AOT 对 Compose 不可用 | 🟡 无改善 |
| **Electron** | 用户明确排除（WebView/Chromium） | ❌ 约束 |

### 7.3 首选方案详述：Qt 6 + QML (C++)

**为什么是 Qt：**

1. **资源占用**：17–30MB，比现有 JavaFX 轻 4–6 倍。对 24/7 常驻应用意义重大。
2. **系统集成无人能及**：
   - `QSystemTrayIcon`：OBS Studio / Bitcoin Core / qBittorrent 生产验证的黄金标准
   - `QWebSocketServer`：直接替代当前 Java WebSocket Server
   - `QProcess`：完整子进程生命周期（对应 Java ProcessManager）
3. **与渲染器同语言生态**：项目渲染器已是 C++17，控制面板也用 C++ 意味着：
   - 零语言切换成本
   - 可共享构建工具链（CMake）
   - 未来若需深度集成（如进程内嵌入）路径畅通
4. **现代 UI**：MultiEffect（模糊/阴影/着色单 shader）+ QML 声明式动画 + 热重载预览，显著优于 JavaFX
5. **生态健康**：35 年历史，The Qt Company 上市公司（Nasdaq Helsinki），KDE 全桌面基于它，Telegram/VLC/OBS 在用，LTS 支持至 2029

**接受的代价：**

| 代价 | 缓解 |
|:---|:---|
| 系统事件需 ~200 行平台代码 | QHotkey 库（662★）+ `QNetworkInformation`（原生）+ `QDBusConnection`（Linux）|
| LGPL 动态链接需合规 | 动态链接 + About 对话框声明 + Qt 源码托管，可商用 |
| QML↔C++ 桥接学习曲线 | 2–4 周上手（C++ 开发者）/ 1–2 周（JS/Web 背景者）|

### 7.4 语言选择建议

| 语言 | 资源占用 | 开发效率 | 推荐场景 |
|:---|:---|:---|:---|
| **C++（推荐）** | ✅ 最佳 | 中 | 与渲染器同语言，追求最佳性能和最小体积 |
| **Python (PySide6)** | ⚠️ ~100MB | ✅ 高 | 快速原型/脚本密集逻辑，但失去资源优势 |

---

## 八、关键风险与前置验证

选定技术栈后，以下风险项应在启动前验证（PoC 级别）：

| # | 风险 | 验证方式 | 影响 |
|:---:|:---|:---|:---|
| R1 | Wayland 上的窗口行为 | 在 GNOME-Wayland / KDE-Wayland / Sway / X11 上测试无边框+置顶+逐像素透明 | 高 — Wayland 是 Linux 未来 |
| R2 | 系统托盘跨发行版 | Ubuntu(GNOME) / Fedora / Arch(KDE) 验证托盘图标显示 | 中 — GNOME 需扩展 |
| R3 | 全局快捷键（如需要） | 纯 Wayland 无 `XGrabKey` 等价物，需组合器特定 API | 中 — 影响功能范围 |
| R4 | 子进程树终止 | Windows 需 Job Object，Linux 需进程组；验证框架 API 是否覆盖 | 中 — 影响稳定性 |
| R5 | 多实例 WS 连接隔离 | 验证单 Server 多 Client 的 token 认证与消息路由 | 低 — 当前架构已验证 |

---

## 九、市场验证：同类项目技术栈

调研了现有桌面宠物/覆盖层项目的技术选型，**无共识**，市场分散：

| 项目 | 技术栈 | 备注 |
|:---|:---|:---|
| **VTube Studio**（商业 VTuber 霸主） | Unity + Live2D Cubism SDK | 游戏引擎路线，Steam 发行 |
| **furudbat/wayland-vpets** | C++ + wayland-client | **~8MB RAM**，Wayland 原生覆盖层 |
| **WindowPet** | Tauri + React | 跨平台，点击穿透，托盘，自动更新 |
| **desktop-waifu** | Tauri + GTK4 Layer Shell + React | Wayland-only，VRM 3D 模型 |
| **X-T-E-R/OpenPet** | Tauri + TypeScript + Rust | 透明置顶，HTTP/MCP API |
| **Ice-teapop/desktop-pet** | Electron + React | macOS NSPanel 透明 |
| **zfhooyfincasnpktlmeisxv/DesktopPet** | PyQt6 | Windows 透明托盘小游戏 |

**关键洞察**：
1. 商业 Live2D 工具用**游戏引擎**（Unity），因为 Live2D 渲染需要 GPU 管线
2. 认真的跨平台覆盖层项目越来越多选 **Tauri**（但被本项目排除——WebView）
3. 最干净的原生覆盖层用 **C++ + Wayland 直接**（8MB RAM），但牺牲可移植性
4. **本项目架构（分离渲染器+控制器）是正确的**——desktop-waifu 采用了相同模式（Tauri 启动器 + 独立 Rust GTK 覆盖层）

---

## 十、参考来源

### 性能基准

| 来源 | 覆盖 | 链接 |
|:---|:---|:---|
| szibele.com | Linux 同机多框架内存对比 | szibele.com/memory-footprint-of-gui-toolkits/ |
| cross-platform-test | Windows 同机 Qt/Flutter/Electron 对比 | github.com/zxxxxxxxx/cross-platform-test |
| anthonytietjen.blogspot.com | Windows Qt/Flutter/Electron 文件大小与内存 | anthonytietjen.blogspot.com |
| chenguangliang.com | Flutter vs Electron 深度对比(2026) | chenguangliang.com/en/posts/blog172 |
| rproc dev.to | egui→Slint 迁移：135MB→30MB | dev.to/trystan_sarrade |
| JB#1632 | Compose MP Hello World 内存分解 | github.com/JetBrains/compose-multiplatform/issues/1632 |
| Zhuravlev Medium | Compose MP RSS 383MB 分解 | medium.com/@zhuravl321 |
| Hicron Software | Avalonia vs Electron 企业对比(2026) | hicronsoftware.com/blog/avalonia-ui-for-enterprise-applications |
| Flutter#179834 | Flutter Windows GPU 内存泄漏 | github.com/flutter/flutter/issues/179834 |
| Avalonia#16013 | Avalonia 空窗口膨胀 7.4GB | github.com/AvaloniaUI/Avalonia/issues/16013 |

### 框架深度调研

| 框架 | 关键来源 |
|:---|:---|
| Qt 6 | doc.qt.io（官方文档）、QHotkey(github.com/Skycoder42/QHotkey)、KDE Discuss（Wayland 限制）|
| Flutter | flutter#66751/#183558/#184366（透明窗口 blocker）、softaims.com（诚实评测）、pub.dev（window_manager/tray_manager）|
| Avalonia | docs.avaloniaui.net、github.com/AvaloniaUI/Avalonia（TrayIcon/Window 源码）、Velopack docs |
| Slint | docs.slint.dev、slint#9507（wgpu 透明 bug）、slint#10837（内存基准）、slint.dev/pricing（许可证）|
| Compose MP | JB#1632/#2436/#1460（内存）、xckevin.com（Electron→Compose 迁移）、blog.jetbrains.com（Toolbox 案例）|
| GTK4+gtk-rs | gtk-rs book、gvsbuild README（Windows DLL）、HN 2025（GTK4 内存退化）|

### 社区对比与市场验证

| 来源 | 覆盖 | 链接 |
|:---|:---|:---|
| youngju.dev | 2026 桌面框架深度对比 | youngju.dev/blog/culture/2026-05-14 |
| softaims.com | Flutter Desktop 生产就绪评估(2026) | softaims.com/blog/flutter-web-desktop-production-ready-2026 |
| yalovoy Medium | 5 框架同应用对比（Tauri/Slint/egui/Dioxus/Flutter）| medium.com/@yalovoy |
| Sparkles | 窗口系统集成对比（Wayland 支持矩阵）| sparkles-docs.pages.dev |
| scvalex.net | Slint 实战评价 | scvalex.net/posts/72 |

---

> **文档版本**：v1.0 · 调研日期 2026-07-13
> 如候选框架发布重大版本（如 Flutter 合并 #184366、Avalonia NativeAOT 成熟、Slint 生态完善），本文档需重新评估。
