# 组合重构设计：方案 A 插件架构 × 去 Python 纯 CMake 构建

> **状态**: 已执行至 P6a ★冻结点（2026-09-19）——P0/P1a-d/P2/P3/P4/P5/P6a 全部完成并提交；SDK 冻结为 API 1.0（提交 b446040，报告见 baselines/p6a-freeze-2026-09-19.md）。**P6b（平台下载器插件）按用户指示暂停未启动**，恢复时见冻结报告 §5 就绪清单。
> **日期**: 2026-09-19
> **性质**: 开工级决策文档。评审通过后 P0 启动；三个"已定口径"（§E 末尾）可推翻，推翻需回改本文档。
> **范围**: controller_qt/ 架构（API 层 + 插件系统）+ 仓库级构建工具链（build.py → CMake）

---

## 决策记录

| 项 | 结论 |
|---|---|
| 插件架构 | 三方案对比（A 进程内 C++ / B QJSEngine 脚本 / C 独立进程 IPC）后选定 **A**。理由: 项目本身需要全面重构（含构建工具链），A 的"必须先拆库"前置成本成为计划内投入 |
| 构建工具链 | 移除 build.py（Python），全面迁移 CMake（CMakePresets） |
| 第一插件 | 第三方平台的模型/语音包下载器（网络下载 + 解压落盘 + 面板 UI 页面） |
| 方案 A 已知代价（接受） | 第三方插件崩溃无隔离（违反蓝图 §9.5 字面义，以"第一方插件=面板组成部分"语义接受）；MinGW 13.1.0 ABI 锁定（以元数据门控 + SDK 模板缓解） |
| 评审遗留决策（2026-09-19 拍板） | ① P1d 最小 CI **采纳**（计入总量）；② poc 目标随 P2 **退役删除**；③ qml module backing target **归 pet_panel_core**（core 接受 Qt6::Quick 依赖） |

**三方案能力上限对比（决策依据摘要）**

| 维度 | A: 进程内 C++ | B: QJS 脚本 | C: 独立进程 |
|---|---|---|---|
| 崩溃隔离（§9.5） | ✗ 无（崩溃即面板死） | △ 半（死循环/OOM 不可） | ✓ 完全 |
| ABI 稳定性 | ✗ 最差（MinGW/Qt/构建类型全锁死） | ✓ 无 ABI（纯文本） | ✓ 协议版本化，语言无关 |
| UI 表达力 | ✓ 天花板=宿主本身（真 QML+信号槽） | △ 声明式目录 | △ 声明式目录 |
| 语言/分发 | 仅 C++ 且须同工具链 | 纯 JS 文本 | 任意语言 |
| 库拆分前置 | 必须（41 测试重构） | 不需要 | 不需要 |

用户评估: 既选 A，则 UI 上限与原生能力全额兑现；崩溃隔离缺失以"结构性收窄 + 流程保证"补偿（见 §A.1）。

---

## 0. 基线事实（全部经代码实证）

| # | 事实 | 来源 |
|---|---|---|
| F1 | main.cpp 509 行 god-wiring: 栈对象按严格顺序手工接线，QML context property **17 个**集中注册（227–348 行）〔v2 修正: 原稿记 14 个/227–311〕 | 实测复核 |
| F2 | WsServer 三闸门（Origin/instance_id/token），连接表 `QMap<int, QWebSocket*>` 以 int instance_id 为路由身份 | 抽查 |
| F3 | 生产侧**两个** qt_add_executable（主目标 + `desktop-pet-controller-qt-poc`，CMakeLists.txt:53/338〔v2 修正: 原稿漏记 poc〕），无生产 add_library；41 个测试各自重编译所测 .cpp（tests/CMakeLists.txt 2053 行） | 实测复核 |
| F4 | 主目标已链接 Qt6::Network；controller 目前零 HTTP 客户端 | 抽查 |
| F5 | build.py 12 项职责仅 5 项需迁移（PATH 注入 / configure 参数 / windeployqt / GLEW+GLFW 下载 / cubism 守卫）；交互菜单与彩色输出直接丢弃 | explorer 盘点 |
| F6 | 测试侧 windeployqt 出现 **152 次**（41 个测试块各自复制 POST_BUILD），ctest 运行期 Qt DLL 全靠它们 | explorer 盘点 |
| F7 | PathResolve 回退 = `<exeDir>/../build/bin`（exe 在 build/controller_qt 下一层）+ 4 个测试烘焙 `BIN_OUTPUT_DIR` 绝对路径 → presets 的 binaryDir **必须沿用现有目录名** | explorer 盘点 |
| F8 | 拆库收益: 41 测试现 ~150 编译 TU → 拆后 ~60；Envelope.cpp 被重编 17 次 / WsServer 9 次 / Protocol 8 次；InstanceSessionTest 与 InstanceManagerTest 各链 ~29-30 生产 TU | explorer 盘点 |
| F9 | Python 残留仅两处: fetch_cubism_core.sh 的 python3 heredoc + run_ui_tests.py（UI 自动化回归基线）；无 CI/pre-commit 隐藏依赖 | explorer 盘点 |
| F10 | Ninja 单配置语义: controller 走 CMAKE_BUILD_TYPE，build.py 的 `--config Release` 是多配置残留 | explorer 盘点 |
| F11 | tests/CMakeLists.txt **141 处** `${CMAKE_SOURCE_DIR}/src/...`（含 include 目录风格共 180 行〔v2 修正: 原稿记 41 处〕）+ 4 处 BIN_OUTPUT_DIR 定义 + 1 处 CUBISM_RESOURCES_DIR 依赖"CMAKE_SOURCE_DIR == controller_qt/"前提——根聚合即静默断裂; 运行时代码零环境变量**读取**（仅 main.cpp:49 一处 qputenv 写入；C.2 的 QT_MINGW_ROOT 将是第一个构建期环境变量） | 实测复核 |

---

## A. 方案 A 遗留风险的一等约束设计

### A.1 崩溃隔离边界：接受"第一方插件 = 面板组成部分"，不做运行时包裹

| 措施 | 表态 | 理由 |
|---|---|---|
| MinGW 下 SEH `__try/__except` 包裹插件调用 | **不做** | mingw-w64 GCC（含 13.1.0）不支持 `__try` 关键字（MSVC 扩展）。SIGSEGV 信号处理在栈已坏时无法安全恢复 C++ 对象。做了 = 假安全感，比没有更危险 |
| 进程内看门狗（超时检测/中断） | **不做** | 只能检测不能恢复；进程内无安全中断手段；复杂度/收益比极差 |
| 语义定位 | **第一方插件与 InstanceSession 同信任级**：崩溃即面板退出，与现状一致 | 不假装隔离。写进 SDK README 与架构文档，禁止后来者误以为存在运行时隔离 |
| 结构性收窄（真正防线） | 危险能力 100% 宿主化（§B.4）+ 插件代码量最小化 | 下载器最危险路径（网络/解压恶意 zip/落盘）全在宿主受测代码；插件只剩目录知识+元数据映射，爆炸半径从"整个网络与文件系统"缩到"一次 QString 解析" |
| 异常层面 | IPluginContext 调用边界 **catch 所有异常**（沿用 MessageDispatcher"坏处理器不毒化消息泵"惯例） | 能拦的异常全拦（解析/逻辑错误）；拦不住的（内存损坏/死锁）诚实接受 |
| 崩溃转储（v2 新增） | **做**: Win32 `SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，只转储不恢复 | MinGW 可用（Win32 API，与被拒的 `__try` 无关）；"崩溃即退出"成立的前提是事后可诊断，否则每次插件崩溃都是无尸检的死亡；成本约半天 |
| 流程性补偿 | 插件同仓、同 review、本地 ctest 全绿门禁；CI 现状缺失（F9 实证仓库无任何 CI）——P1d 最小 CI 落地后升级为"同 CI"门禁 | A 的隔离是流程保证而非结构保证——A 的固有税，文档明示〔v2 修正: 原稿"同 CI"为空头支票〕 |

### A.2 MinGW 13.1.0 ABI 门控：元数据先于实例化的硬校验

`Q_PLUGIN_METADATA` 增补 `abi` 块（Qt metadata JSON 自由字段，`QPluginLoader::metaData()` 在 `instance()` 前读取——先验元数据、后实例化）:

```
"abi": { "compiler": "gcc", "compiler_version": "13.1.0",
         "qt_version": "6.10.0", "qt_build": "mingw_64", "build_type": "Release" }
```

| 字段 | 门控规则（PluginRegistry::validate，加载期执行） |
|---|---|
| `compiler` + `compiler_version` | 严格相等，否则拒绝（错误码 `PLUGIN_ABI_MISMATCH`，进插件管理 UI，不静默） |
| `qt_version` | 同 major 下 plugin minor ≤ host minor；patch 任意〔v2 修正: 对齐 Qt 官方"同 major 向后二进制兼容、patch 双向兼容"保证——原稿"minor 相等"严于官方保证且无必要，徒增插件维护税〕 |
| `build_type` | 严格相等〔v2 修正理由: "MinGW Debug/Release 混链即崩"是 **MSVC 专属**规则——Qt 5.14 起 MinGW debug DLL 不加 `d` 后缀、同链 msvcrt；MinGW 真正的 ABI 风险是不同 GCC 版本的 libstdc++，已由 compiler_version 字段拦截。保留严格相等是保守门控 + 为 MSVC 逃生门预留一致性〕 |
| `api_version` | host_major ≥ plugin_major 且 host_minor ≥ plugin_minor（OBS 非对称规则） |

- **编译期插件豁免** abi 校验: 同仓同次构建，字段由 CMake 构建时生成注入（§B.6），不手写。
- **SDK 构建模板**: `plugins/sdk-template/` 自带 CMakePresets.json，preset 继承宿主 `cmake/qt-mingw-qt.cmake`（§C.2）——第三方拷模板即得正确工具链，把 ABI 锁死从文档陷阱变成默认正确。
- **MSVC 逃生门**: ① 接口纪律（§B.3）禁 `std::` 跨界/导出模板，只留纯虚 + Qt 值类型 + POD → 切 MSVC 时 exe+插件全量同步重编即可，零源码改动；② abi 门控保证 MSVC 旧插件被明确拒绝而非静默加载后内存损坏；③ 构建面只加一个 toolchain + 一个 preset。Qt 官方 Windows 主推 MSVC，未来迁移概率不低——接口纪律因此是一等约束。

### A.3 分阶段: 编译期插件（静态）→ 动态 QPluginLoader

**表态: 第一阶段只做静态"编译期插件"；接口按动态标准设计；开启判据如下。**

| 维度 | 阶段 1: 编译期插件 | 阶段 2: 动态 QPluginLoader（未来） |
|---|---|---|
| 形态 | `plugins/<name>/` 源码目录 → STATIC 库链进 exe，自注册宏构造工厂表 | 独立 `.dll` + QPluginLoader + abi 门控激活 |
| ABI 风险 | **零**（同一次编译） | pet_panel_core 需 SHARED 化 + 全套二进制兼容纪律 |
| 崩溃语义 | = 面板组成部分（A.1 已接受） | 依旧零崩溃隔离——动态化的动机是"不经面板发版装插件"，从来不是隔离 |
| 接口 | 同一套 IPanelPlugin，只换加载器 | 同左 |

**开启阶段 2 的判据（v2 扩为四条，全满足才动）**: ① 出现第一个不同仓构建的插件需求；② SDK 接口经 ≥1 个真实插件打磨后冻结（v2 量化: API 无 breaking 变更连续 ≥3 个月）；③ 团队接受发 SDK + abi 维护税，且 Qt/MinGW 升级节奏稳定；④ 团队接受进程内能力门控仅为建议性约束（§B.4 v2 修正）。在此之前 SHARED 化是纯预付成本。阶段 1 唯一不能省的前置投资 = 接口纪律按阶段 2 标准执行，保证未来切换是"换加载器"而非"改接口"。

### A.4 热卸载: 明确禁止

加载单向；禁用/启用 = 写配置 + 提示重启生效（Qt Creator/OBS/KDE 一致结论: unload + 活信号连接/QML 实例 = 悬挂指针）。静态阶段本无卸载概念，此纪律为阶段 2 预立。

---

## B. 架构细化

### B.1 库拆分: 两个库 + exe 壳，第一方插件为 STATIC 库

```
controller_qt/
├── CMakeLists.txt              # find_package + add_subdirectory(api core app plugins tests)
├── src/api/                    # pet_panel_api (INTERFACE, header-only)
│   ├── IPanelPlugin.hpp / IPluginContext.hpp
│   ├── IInstanceApi / IDownloadApi / IVoicePackApi / IUiApi .hpp
│   └── PluginTypes.hpp         # POD + Qt 值类型（InstanceInfo / DownloadRequest / ...）
├── src/core/                   # pet_panel_core (STATIC；阶段 2 切 SHARED)
│   └── 现有 core/network/ui/system 全部实现 + 新增 facade/download
├── src/app/                    # exe 壳: main(≤100 行) + PanelApplication + ScreenshotRunner
├── plugins/
│   ├── downloader/             # 第一方插件（STATIC 库 + 自注册）
│   └── sdk-template/           # 第三方模板（阶段 2 起可独立构建）
└── tests/                      # 41 测试 → target_link_libraries(pet_panel_core)
```

| 目标 | 类型 | 理由 |
|---|---|---|
| `pet_panel_api` | **INTERFACE（纯头）** | Qt 插件接口行业标准形态；零链接产物，永不出现两份接口实现；静态/动态两阶段共用 |
| `pet_panel_core` | **STATIC →（阶段 2）SHARED** | 单副本链进 exe。从第一天按"未来 SHARED"的边界纪律建设: 库内禁单例、禁全局状态、跨 TU 的 QObject 元对象只注册一次——STATIC→SHARED 切换的质量取决于这条。**qml module backing target 归此库（已拍板）**: core 接受 Qt6::Quick 依赖——ui/ 目录本就在 core，注册点搬迁变为库内搬家，P2 断裂面更小 |
| exe 壳 | executable | 509 行 god-wiring 的归宿（B.2） |

**poc 目标处置（v2 新增；已拍板 2026-09-19）**: 现存第二个生产 exe `desktop-pet-controller-qt-poc`（CMakeLists.txt:338，F3）为 Phase 0 里程碑验证工具（T6 产物，无 GUI 控制台，驱动 renderer 跑 WS 往返）。**决定: 随 P2 退役删除**——该路径已被 HandshakeTest/WsServerTest 覆盖；P1a/P1b 期间保持原样构建，产物等价清单显式包含 poc exe；P2 删除目标 + `src/poc_main.cpp`。

**41 测试改造**（tests/CMakeLists.txt，2053 行 / 152 处 windeployqt / 41 个 qt_add_executable）: 每个测试的显式 `.cpp` 列表整体替换为 `target_link_libraries(... pet_panel_core)`；qt_add_library 后 Q_OBJECT moc 产物随库导出，测试只链接即可。

**编译收益（F8）**: 41 测试 ~150 编译 TU → 拆后 ~60。门禁仍以 P0 实测基线对比验收。

### B.2 组合根拆分: main.cpp 509 行 → 四步，每步可验收

方向: **QObject parent 树替代栈逆序析构纪律**，engine 仍最后创建。纯搬家，不改任何行为（context property 名、connect 顺序逐字保留——既有 stack-ordering 注释里的教训不能丢）。

**析构顺序假设（v2 标注）**: QObject 子对象按注册逆序析构是 Qt 6.6+ 的**实现行为**（Qt 6.10.2 实测与栈逆序吻合），非文档化契约——等价性仅在单亲树内成立；跨子树（PanelApplication 树与 UI 桥对象）的相对析构顺序必须在 parent 挂载点显式设计。且现状 `--screenshot` 路径以 `std::exit()` 绕过 teardown（main.cpp L495–502，注释明言销毁 engine 会堆损坏），退出路径今天就没有任何自动化覆盖。

**退出 QA（v2 新增，P0 起为门禁）**: exe 增隐藏参数 `--self-quit <ms>`（M4 归位时实现），脚本启动真 exe → 正常 close 路径退出 → 断言退出码 0，双平台。P0 记录现状退出行为基线；M 系列每步后必跑，M2 起要求退出码 0（若 P0 基线即异常，M2 须修复后达标）。

| 步 | 内容（main.cpp 行号） | 验收 |
|---|---|---|
| M1 | `ScreenshotRunner`/bubble QA（379–504，~125 行）→ `src/app/ScreenshotRunner.cpp`，main 留 3 行调用 | `--screenshot`/`--bubble` 截图 diff 等价 |
| M2 | `PanelApplication`: DatabaseManager/PanelStateManager/WsServer/PendingRequests/InstanceManager 及其 connect（85–159）→ parent 树（L139–144 的 parent=nullptr + qDeleteAll 防双删注释随搬家同步改写——对象改堆+parent 后该前提消失） | 41 测试绿 + 手动冒烟 + 退出 QA |
| M3 | UI 桥（tray/autoLaunch/panelConfig/notificationStream/voicePacks/assetManager + applyLogo，171–314）→ `PanelUiBoot::registerAll(engine)`；context property 名不变，QML 零改动 | 同上 + 全页面 `--screenshot` 绿 |
| M4 | 冷启动锚点/参数解析归位，main ≤100 行 | LOC 断言脚本 |

每步一个 `refactor(controller_qt): ...` 提交，ctest 全绿 + 退出 QA 为门禁。

### B.3 SDK 接口草案（方法签名级）

**ABI 纪律（写进 src/api/README，review 强制）**: 跨界仅纯虚接口、Qt 值类型（QString/QVector/QJsonObject）、POD、`const&`/值返回；**禁** `std::` 类型（MinGW 跨 DLL 的 std ABI 陷阱）、禁导出模板、禁接口头内 inline 实现（防跨 DLL ODR/导出问题；vtable 稳定来自"不改/不重排虚函数声明"——v2 修正原稿理由错挂）。

```
IPanelPlugin（每插件一个实现；Q_DECLARE_INTERFACE 声明 iid）
├─ initialize(IPluginContext&) → PluginError   // 唯一入口；宿主 catch 全部异常
├─ shutdown()                                  // 退出时逆序调用；≤200ms 预算，禁重活
（metadata 由构建系统生成的 plugin.json 提供，不占接口方法）

IPluginContext（宿主实现，构造注入）
├─ instanceApi() / voicePackApi() / uiApi() → 各接口引用
├─ downloadApi() → IDownloadApi&               // 仅当 manifest capabilities 含 "network"
├─ pluginConfigDir() → QString                 // <configDir>/plugins/<id>/ 私有可写区（写规约 v2: JSON + QSaveFile 原子写 + snake_case，同项目配置反模式，进 SDK README）
├─ log(level, msg)                             // 走宿主 spdlog，自动带插件 id 前缀

IInstanceApi（只读 + 订阅；刻意无命令/配置写口——YAGNI，每方法都是永久承诺）
├─ instances() → QVector<InstanceInfo>          // {uuid, label, modelName, status, connected}
├─ subscribeRoster(IRosterObserver*)            // v2 修正: 原稿 std::function 违反本节"禁 std:: 跨界"纪律
├─ unsubscribeRoster(IRosterObserver*)          // IRosterObserver: 纯虚 rosterChanged()；GUI 线程回调，插件须速回

IVoicePackApi
├─ listPacks() → QVector<PackInfo>             // 复用 VoicePackScanner
├─ refreshScan()                               // 安装完成后触发哨兵扫描
├─ installPath() → QString                     // Resources/VoicePacks

IDownloadApi（宿主咽喉点，全异步）
├─ start(DownloadRequest{url, expectedSha256, destName}, DownloadListener*) → JobId
│    // Listener: onProgress / onFinished / onError（GUI 线程）
├─ cancel(JobId)
├─ installArchive(JobId, InstallSpec{targetDir})  // 校验→解压→zip-slip 防护→原子落盘
│    // 与 start 分离: 下载完成 ≠ 安装（用户确认在中间）

IUiApi
├─ registerPage(PageDescriptor{title, iconUrl, qmlUrl, order})
├─ notifyBubble(text, durationMs)              // 复用 NotificationStreamController
│    // 无 unregisterPage/页面生命周期——刻意 YAGNI（禁用=配置位+重启，§B.6 自洽）；v2 标注防 P6a 重议
```

**生命周期**: 宿主启动 → Registry 校验 → 按 manifest 顺序 `initialize()`（逐个 try/catch，失败标记 Failed 不阻塞后续）→ 运行 → 退出逆序 `shutdown()`。**线程契约**: 回调全在 GUI 线程（QNAM 异步信号驱动天然满足）；插件回调内禁阻塞，写进 SDK 文档。

### B.4 DownloadService 宿主化: 全进程唯一网络合法路径（v2 修正标题——"插件不能自开网络"对进程内插件不成立）

| 理由 | 说明 |
|---|---|
| 规约统一 | QSaveFile 原子写、.part 暂存、sha256、zip-slip 规范化+前缀检查是项目级规约；放进插件 = 每插件自实现一遍"加密正确的解压器"，必然漂移 |
| 咽喉点审计 | 全进程**唯一合法** QNetworkAccessManager 在 DownloadService；manifest `network` capability 决定 downloadApi 可用性（无则 stub，调用返回 ERR_CAPABILITY）。**v2 诚实度修正**: capability = 用户同意展示 + 审计清单 + 第一方 review 门禁，对第三方是**建议性约束非执行机制**（进程内 C++ 可自开 socket）；对第一方插件吊销网络有唯一开关，对第三方的硬吊销需回到方案 C 进程边界——如实写进 SDK README |
| A 特有 | 进程边界不提供能力隔离（A.1），能力边界由 API 面提供**合法路径与审计**〔v2 修正: 原稿"必须由 API 面提供"暗示强制力，属过度声明〕 |

管线（全宿主受测代码）: `url → QNAM(HTTPS) → .part 流式落盘+sha256 实时计算 → 校验 → 解压到临时目录（逐文件原子写） → cleanPath 规范化 + 前缀检查（拒 ../绝对路径/盘符） → 移动到 Resources/VoicePacks/<名>/ → meta.mko 哨兵就位 → refreshScan()`

### B.5 插件 QML 页面注册: 宿主引擎 + Loader，不用 engine->load

- **`Loader { source: page.qmlUrl }`**，不是 `engine->load`（后者创建第二个顶层窗口、脱离 StackView 页面路由）。
- **暴露面控制**: 为每页新建 `QQmlContext(rootContext)` 子 context，只 setContextProperty("plugin", pluginBridge)。**已知债务（诚实标注）**: QML context 链继承，root 的 17 个 context property（F1 v2 修正）对插件 QML 仍可见——阶段 1 接受（第一方可信），阶段 2 需评估独立 QQmlEngine 或 C++ 桥接收紧。写进架构文档，不隐藏。
- Theme 单例自动继承（同引擎）；SDK 模板提供标准页面骨架（含 FluFrame 需显式高度等既有陷阱的规避示例）。
- **热重载口径（评审 Q&A）**: C++ 插件体禁热卸载（悬挂指针；Qt Creator/OBS/KDE 一致），"重启生效"是正式模型——启用/禁用走配置位+重启，阶段 2 新增/更新插件 = 放置/替换 dll+重启。**开发期折中**: 插件 QML 页面经 Loader 加载，可安全刷新（重新 setSource 即时生效 UI 改动，C++ 体不动），插件迭代大头落在此层。
- **路径属性契约（评审 Q&A）**: 现有 QML 三处 `"file:///" + 路径` 手工拼 URL（WelcomePage.qml:198 / SettingsPage.qml:418 / VoicePackPage.qml:61）对空格/中文未 percent-encode——插件与宿主的路径属性一律以 `QUrl::fromLocalFile()` 预转换后暴露（url 变体属性），QML 不自行拼 `file:///` 前缀；存量三处搭 P2 顺带修。

### B.6 PluginRegistry: manifest + 状态机 + 发现目录

| manifest 字段 | 说明 |
|---|---|
| `id` | 反向域名，唯一键 |
| `version` / `api_version` | 插件 semver / 所需 API major.minor |
| `abi` | A.2 四字段（CMake 构建时生成注入，不手写） |
| `capabilities` | `["network","voicepacks_install"]` 白名单 |
| `entry.qml` / `title` / `icon` / `order` | UI 注册。**资源布局（v2 新增）**: 插件 QML/icon 经各自 `qt_add_qml_module` 打 qrc 进插件 STATIC 库，`qmlUrl`/`iconUrl` 用 `qrc:/` URL——不依赖文件系统部署路径，规避 PathResolve 布局敏感性（F7）；阶段 2 独立 dll 再评估文件系统布局 |
| `min_host_version` / `vendor` / `signature`（阶段 2） | 门控与发布安全 |

**发现目录**: 阶段 1 唯一来源 `controller_qt/plugins/`（CMake 收集）；阶段 2 增加 `<exeDir>/plugins/` + `AppDataLocation/plugins/`（OBS 布局 `<Name>/bin+data`）。

**状态机（阶段 1 简化）**: `Discovered → Validated → Registered → Started → (Stopped | Failed)`；Failed 带错误码进 Settings 新增的插件管理节（启用/禁用/状态/错误；禁用 = 配置位 + 重启提示）。阶段 2 增补 `Loaded/Crashed` 前置语义。

**两阶段加载**: CMake **configure 期**收集 `plugins/*/plugin.json` 做**字段存在性 + api_version 数值**校验（`string(REGEX)` 级——CMake 无 JSON 解析，完整 schema 校验在运行期 Registry；v2 修正原稿"schema 校验"措辞）（损坏 = configure 报错，fail fast 最早化）→ 构建期生成 `static_plugins.cpp` 工厂表（id → create()）→ 运行期 Registry 逐个 initialize。

---

## C. 构建重构细化（去 Python → 纯 CMake）

### C.1 CMakePresets.json 布局与用户入口

**表态: 不做伞形顶层 CMakeLists**（双 MinGW 决定 Windows 上必须两次独立 configure——一个 CMake 实例一套编译器）。每组件一份 presets（schema v6，构建机 CMake ≥3.25，写进 BUILD.md 首节）+ 顶层薄壳脚本保留一键体验。

**binaryDir 硬约束（F7）**: 必须沿用现有目录名 `build/controller_qt`、`build/renderer_mingw`、`build/renderer_vulkan_mingw` 等——PathResolve 的 build-tree 回退依赖"exe 在 build/controller_qt 下一层"，且 4 个测试烘焙 `BIN_OUTPUT_DIR=${CMAKE_SOURCE_DIR}/../build/bin` 绝对路径。改名 = 同时改 PathResolve + 4 个测试，零收益。

| 组件 | configurePresets | 要点 |
|---|---|---|
| controller_qt | `win-qt-release` / `win-qt-debug` / `linux-qt-release` / `linux-qt-debug` | Ninja；toolchainFile（C.2）；binaryDir `${sourceDir}/../build/controller_qt`；cacheVariables `CMAKE_BUILD_TYPE`（Ninja 单配置: 无 --config，F10） |
| renderer | `win-gl-release` / `win-vk-release`（`USE_VULKAN=ON`）/ `linux-gl-release` / `linux-vk-release` | 保留 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` 进 cacheVariables（build.py 现职责）；binaryDir 沿用 `build/renderer_mingw` 等 |

buildPresets（绑定 configurePreset + jobs）；testPresets（`outputOnFailure` + `noTestsAction:error`——41 测试缺一即失败）；workflowPresets: `win-release`（configure→build→test）、`linux-release`、`test-only`。

**用户入口对照**:

| 旧 | 新 |
|---|---|
| `python build.py qt` | `cd controller_qt && cmake --workflow --preset win-release` |
| `python build.py renderer` | `cd renderer && cmake --workflow --preset win-gl-release` |
| `python build.py`（交互菜单） | `scripts/build.sh|bat`（~10 行纯转发，顺序跑两个 workflow） |
| `cd renderer/build && ctest` | `ctest --preset <test-preset>` |

附带收益: Qt Creator/VS/CLion 原生读 CMakePresets——IDE 打开即正确工具链，PATH 雷区在 IDE 场景同样消失（build.py 时代做不到）。

### C.2 双 MinGW: toolchain file 钉绝对路径（根因修复，非 PATH 过滤）

**表态: toolchain file，不用散装 cache 变量。** 绝对路径使 PATH 顺序不再影响结果——build.py 的 env 过滤是治症状，这是治根因。

```
cmake/qt-mingw-qt.cmake（controller_qt）
  CMAKE_C_COMPILER   = C:/Qt/Tools/mingw1310_64/bin/gcc.exe    （可被 $env{QT_MINGW_ROOT} 覆盖）
  CMAKE_CXX_COMPILER = C:/Qt/Tools/mingw1310_64/bin/g++.exe
  CMAKE_MAKE_PROGRAM = C:/Qt/Tools/Ninja/ninja.exe
  CMAKE_PREFIX_PATH  = C:/Qt/6.10.0/mingw_64
cmake/toolchain-mingw-renderer.cmake（renderer，系统 MinGW，路径经环境变量注入默认值）
```

① 三值（compiler/make/Qt prefix）强相关必须同源，toolchain file 是唯一单点；② preset 一行 `toolchainFile` 引用；③ **configure 期硬校验**: `CMAKE_CXX_COMPILER` 路径不含 `mingw1310_64` → `message(FATAL_ERROR)` 引导——把 AGENTS.md 的"已知陷阱"从文档约定升级为构建期门禁。个人机器差异**首选 `QT_MINGW_ROOT` 环境变量**（toolchain 内已留 `$ENV{}` 缝——presets 覆盖 toolchain 内部变量的机制别扭，v2 明示）；`CMakeUserPresets.json`（gitignore）inherits 作为备选，不发明新配置层。MSVC 逃生门 = 新 toolchain + 新 preset，存量不动。

### C.3 windeployqt 统一函数（新增工作项，F6）

现状: 主 exe 的 windeployqt 在 build.py；41 个测试块各自复制 POST_BUILD windeployqt（152 处出现）——ctest 运行期 Qt DLL 全靠它们。preset 只解决编译期，运行期依赖这批块。

**设计**: `cmake/DeployQtTest.cmake` 提供 `pet_deploy_qt_test(<target>)` 函数（封装 find_program(windeployqt) + POST_BUILD + WIN32 条件 + 幂等提示），41 处调用点替换为单行函数调用。**置于 P1b、先于库拆分**: 先收缩 tests/CMakeLists.txt（预计 -1000 行级），P2 的链接改造 diff 因此更小、review 更容易；函数化与拆库改的是同一文件的不同关注点，分两个提交避免混合。验收: Windows 上 41 个测试 exe 旁 DLL 清单与现状等价（抽查 diff）+ ctest 全绿。

### C.4 bootstrap 顺序: CMake 化到底，但 CMake 不变更 git 状态

**表态: `cmake -P scripts/Bootstrap.cmake` 单文件双平台，取代 fetch_cubism_core.sh/.bat；submodule init 保留守卫+报错提示，不进 CMake。**

| 决策点 | 表态 | 理由 |
|---|---|---|
| Core 抓取 | `file(DOWNLOAD)` + `file(ARCHIVE_EXTRACT PATTERNS "*/Core/*")` + 平台文件校验清单（沿用现 .sh 的 4 个 marker）；幂等 = `if(NOT EXISTS)` + 缓存变量；`CUBISM_SDK_VERSION/URL/FORCE` 环境变量语义保留 | 消灭 .sh 内嵌 python3 heredoc（去 Python 的最后一块拼图）；单文件消灭 .sh/.bat 双实现漂移 |
| GLEW 2.2.0 / GLFW 3.4 | 同一 Bootstrap 函数化（下载→解压→rename 布局不变） | 与 Core 同构，一并收敛 |
| submodule init | **不进 CMake**: renderer configure 期守卫 `file(EXISTS Core/include/Live2DCubismCore.h)` → FATAL_ERROR 提示 `git submodule update --init --recursive` | CMake 不变更 git 状态（职责边界）；隐式网络 IO 不进 configure（离线/CI 缓存透明） |
| 保留的 shell | `scripts/build.sh|bat`（纯转发 ~10 行）、`setup-dev-env.ps1`（装机引导） | 边界声明: shell 只做转发与装机引导，含逻辑（下载/解压/校验）必须 CMake 化 |

新克隆体验 = `cmake -P scripts/Bootstrap.cmake` + 各组件 `cmake --workflow --preset ...`，BUILD.md 重写为两条命令。**run_ui_tests.py 保留**（F9）: 唯一许可的 Python，开发侧 UI 回归工具，不进构建路径。

### C.5 工具链合并选项（P1c，可选后续）

**两套 MinGW 并存是历史而非必然**: controller_qt 的约束来自 Qt（官方预编译 Qt 6.10.0/mingw_64 只保证与 Qt 自带 mingw1310_64 二进制兼容），renderer 无 Qt 依赖、本无理由独立工具链。依赖面盘点: Cubism Core（C API DLL）、GLEW/GLFW（预编译 C 库）、其余全部源码编译——**无 C++ ABI 跨界**。

**做法**: renderer toolchain file 切到与 controller 相同的 `C:/Qt/Tools/mingw1310_64`（toolchain-mingw-renderer.cmake 退役）。

**收益**: 单一编译器确定性（消除"renderer 用哪个 MinGW 取决于 PATH"的可复现性弱点与 Git-MinGW 陷阱根源）；build/bin 单套运行时 DLL（libwinpthread/libstdc++ 冲突消失）；插件 ABI 门控只面对一种工具链。

**不并入 P1a/P1b 的理由**: P1a 验收门是产物等价（DLL 清单 diff 级，见 §D v2），换编译器必然破坏等价性且无法归因（presets 错 or 编译器差异）。关注点分离: 先证明新构建系统忠实复刻，再单独换工具链。

**验收门禁**: renderer 全部测试绿 + OpenGL/Vulkan 双变体截图 QA + 产物依赖 DLL 清单 diff。**风险点**: GLEW/GLFW 预编译包与 GCC 13.1 的链接验证（C ABI 预期无碍，须实测）。**额外代价（v2 新增）**: renderer 从此耦合 Qt 发行节奏——Qt 升级带 MinGW 升级时 renderer 被动跟进；依赖面纯 C ABI，可接受，如实记录。

---

## D. 统一迁移路线图

原则: 先立保护网 → 先换地基（构建）→ 再动结构（库/组合根）→ 后立插件 → 冻结 API。每阶段独立验收、Conventional Commits 单一关注点、测试全绿门禁。

| 阶段 | 内容 | 主要文件 | 验收门禁 | 风险 | 人日 |
|---|---|---|---|---|---|
| **P0 保护网** | 构建时间基线（旧命令实测）；41+renderer 测试全绿快照；`--screenshot` 全页基线；**退出 QA 基线**（`--self-quit` 现状退出行为记录，B.2 v2） | 无改动 | 基线文档入库（含退出行为） | 现状退出路径本身可能异常（记录为已知问题，M2 起修复达标） | 1 |
| **P1a presets+toolchain** | 双组件 CMakePresets（4+4 configure / build / test / workflow）+ 两个 toolchain file + configure 期 MinGW 硬校验 | 各 CMakePresets.json、cmake/*.cmake | `cmake --workflow` 产物等价〔v2 修正: 逐字节等价不可达——exe 内嵌 binaryDir 绝对路径/构建时间戳；改为依赖 DLL 清单 diff + exe 大小/符号量级核对 + 41 测试全绿 + `--screenshot` 全页 diff；产物清单显式含 poc exe（F3，P2 退役前仍为交付物）〕；双平台；binaryDir 沿用旧名 | 路径硬编码的机器差异（QT_MINGW_ROOT 环境变量覆盖验证，C.2 v2） | 2–2.5 |
| **P1b bootstrap+卫生** | Bootstrap.cmake（Core+GLEW+GLFW）；renderer configure 守卫；windeployqt 统一函数（152 处→41 行调用）；主 exe windeployqt POST_BUILD；删 build.py；删 fetch .sh/.bat；setup-dev-env.ps1 降级为可选或删除；BUILD.md 重写（含 run_ui_tests.py 的 Windows-only 平台标注与 `.exe` 后缀按平台条件化） | scripts/、renderer/CMakeLists.txt、controller_qt/{CMakeLists.txt,tests/CMakeLists.txt} | 新克隆按 BUILD.md 两条命令全构建成功；构建路径零 Python；Windows 41 测试旁 DLL 等价 | ARCHIVE_EXTRACT 对 Cubism/GLEW zip 的边角（权限/符号链接）；windeployqt 函数化时序 | 3–4 |
| **P1c 工具链合并（可选）** | renderer toolchain 切到 Qt mingw1310_64，双 MinGW 归一（C.5） | cmake/toolchain-mingw-renderer.cmake 退役、renderer presets | renderer 全部测试绿 + OpenGL/Vulkan 双变体截图 QA + 产物 DLL 清单 diff | GLEW/GLFW 预编译包与 GCC 13.1 链接验证（C ABI 预期无碍） | 1–1.5 |
| **P1d 最小 CI（v2 新增，已采纳 2026-09-19）** | GitHub Actions 双平台矩阵（windows/ubuntu）: Bootstrap + `cmake --workflow` + ctest——presets 使 CI 天然复用本地入口；落地后 §A.1 流程性补偿升级为"同 CI" | .github/workflows/*.yml | 双平台 ctest 全绿出现在 CI 报告 | runner 的 Qt 装机路径与 QT_MINGW_ROOT 注入 | 1–1.5 |
| **P2 库拆分** | pet_panel_api(INTERFACE) + pet_panel_core(STATIC) + exe 壳；41 测试改链接；**poc 目标退役删除**（B.1 已拍板，删目标 + `src/poc_main.cpp`）；**qml module backing target 归 core**（已拍板，QML 类型注册点从 main.cpp:340 随迁入库）；tests/CMakeLists.txt 全部 `${CMAKE_SOURCE_DIR}` 统一改 `${PROJECT_SOURCE_DIR}`（F11 地雷: 防根聚合静默断裂，机械替换——实测 141 处/180 行，v2 修正原稿所记 41） | controller_qt/CMakeLists.txt、tests/CMakeLists.txt、src/ 归位 | 测试全绿；构建时间对比 P0 基线（机制预期 ~150→~60 TU，门禁以实测为准）；build/bin 产物等价（poc exe 移除）；grep 确认 tests 无残留 CMAKE_SOURCE_DIR | 隐藏符号冲突/编译定义丢失（逐测试清）；AUTOMOC 在库目标的行为；**QML 类型注册点归属**（v2: qmlRegisterUncreatableType 在 main.cpp:340 / Theme 单例 / QTP0001 布局——症状是运行期 unknown type 而非编译错） | 5–7（v2: 原 3–5 低估） |
| **P3 组合根** | M1→M4 四步拆 main.cpp（B.2，`--self-quit` 于 M4 实现） | src/app/ | 每步测试绿 + 截图 QA + **退出 QA** + COLD_START_MS 不劣化 >5% | parent 树替换栈顺序的生命周期边角（B.2 v2 假设标注 + 退出 QA 兜底；main.cpp 有 teardown 堆损坏前科 L495–502） | 4–5（v2: 原 3–4 略乐观） |
| **P4 插件框架** | PluginRegistry（静态工厂表）+ PluginHost（initialize/shutdown + 异常包裹）+ 插件管理 Settings 节 + IUiApi 页面 Loader 注入 | src/api/、src/core/Plugin*、qml/ | sdk-template 的 dummy 插件: 页面出现、禁用/重启生效、manifest 损坏 configure 期报错、**从 build/bin 直接运行 exe 插件页面可见**（v2: 资源部署管线验收，B.6） | QML 子 context 暴露面（B.5 已标为阶段 1 接受项） | 4–6 |
| **P5 宿主服务** | DownloadService + 安装管线 + IVoicePackApi（挂载链不动，哨兵对接） | src/core/DownloadService.* | 单测含恶意 zip/坏 sha256/断网（QTest——断网路径需自建 mock seam: 注入式 QNAM/mock，v2） | QNAM 首次引入: Windows Schannel vs Linux OpenSSL 的 TLS/代理/重定向差异；never-throws 契约 | 5–7（v2: 原 4–5 偏乐观） |
| **P6a ★冻结点** | SDK 接口头定稿合入（架构定型 PR 边界）+ SDK README（ABI 纪律/崩溃警告/线程契约） | src/api/ | 接口 review 通过；此后 api/ 变更走版本化 | 无（纯文档+review） | 1–1.5 |
| **P6b 第一插件** | plugins/downloader/: 目录知识+搜索+下载编排+安装确认 UI（真 QML 页） | plugins/downloader/ | 端到端: 搜索→进度→安装→VoicePacks 出现新包→可挂载；崩溃路径演练文档化 | 第三方平台 API 现实变化（业务本质风险）——**开放项（v2）**: 鉴权/CDN 签名/分页怪癖未调研，现实或 6–10，平台 API 调研完成后再定承诺 | 4–6（开放至 6–10） |

**依赖链**: P1 →（P1c 可选 / P1d 已采纳，可与 P2 并行）→ P2 → P3 →（P4 ∥ P5）→ P6a → P6b。P1 先行理由: 所有后续迭代跑在新构建系统上，重构期间不吃双份维护成本。**★冻结点 = P6a**: pet_panel_api 头文件进入版本化受控，pet_panel_core 内部仍自由演进。

---

## E. 总量估计与关键不确定性

**总量: 31–43.5 人日**（P0–P6b 全量，v2 修正——原稿 24.5–34；上调项: P0 退出 QA、P2 5–7、P3 4–5、P5 5–7、P6b 列开放、**P1d 已采纳计入 +1–1.5**）。MVP（P0–P2 + 最小 P4/P5 + 下载器，含 P1d）≈ 25–31.5 人日（原稿 19–25）。

| 不确定性 | 影响 | 缓解 |
|---|---|---|
| 41 测试改链接的隐性破坏（2053 行逐目标语义） | P2 ±2 人日（v2: 已按上限计入 5–7） | P0 基线；每批 5–8 测试一提交，bisect 粒度小；C.3 先行收缩文件 |
| QML 子 context 暴露面（17 个 root 属性对插件 QML 可见，F1 v2） | 阶段 2 前的债务 | P6a 文档标注；阶段 2 评估独立引擎；不隐藏 |
| A 方案崩溃语义的现实化（插件 bug = 面板崩） | 用户感知 | A.1 结构性收窄 + P6b 崩溃演练文档；不做假隔离 |
| QNAM 首次引入的跨平台网络行为 | P5 ±1.5 人日（v2: 已按 +1–2 计入 5–7） | 双平台集成用例；never-throws 契约 |
| ARCHIVE_EXTRACT 对大 zip 的边角 + windeployqt 函数化时序 | P1b ±1 人日 | FORCE 重跑语义 + DLL 清单 diff 验收 |
| CMake ≥3.25 构建机升级 | 环境前提 | BUILD.md 首节 + setup-dev-env.ps1 更新 |
| spdlog FetchContent 首 configure 需网络 | 无回归（现状即有） | Bootstrap 文档提及 |
| 退出路径无自动化覆盖（现状即无；screenshot 路径 `std::exit()` 绕过 teardown，v2 新增行） | P3 | P0 退出 QA 基线 + M 步门禁（B.2 v2） |
| Qt 子对象析构顺序为实现细节（6.6+ 实测逆序，非契约，v2 新增行） | P3 后 Qt 升级 | B.2 假设标注 + 退出 QA 回归网 |

**三个已定口径（可推翻，推翻需回改本文档）**:
1. 阶段 1 插件 = 编译期静态（接口按动态标准设计）；
2. configure 期不做隐式网络下载与 git 变更，bootstrap 显式独立；
3. 崩溃包裹/看门狗不做，"第一方插件 = 面板组成部分"写进架构文档与 SDK README。

---

## 评审检查单（建议重点）

1. **§A.1 崩溃语义**: "第一方插件 = 面板组成部分"是否可接受？
2. **§A.3 阶段划分**: 编译期静态插件先行、三条判据后开动态——判据是否合适？
3. **§B.3 接口草案**: IInstanceApi 刻意只读（无命令/配置写口）是否符合预期用途？
4. **§B.1 库粒度**: api（纯头）+ core（STATIC）两库是否足够/过度？
5. **§C.1 入口变化**: `cmake --workflow --preset` 替代 build.py 是否满足日常使用习惯？
6. **§C.2 路径硬编码**: Qt 装在 C:\Qt 的约定 + CMakeUserPresets 覆盖机制是否适配你的机器？
7. **§D 顺序**: P1（构建）先于 P2（结构）——若你更想先看到插件能力，可对调 P1b/P2，代价是重构期间维护双构建入口。
8. **§C.5 工具链合并（P1c，可选）**: 是否采纳"renderer 切到 Qt mingw1310_64、双 MinGW 归一"？收益是单编译器确定性与单套运行时 DLL；代价是 GLEW/GLFW 预编译包需实测验证，且必须在 P1 稳定后独立执行（不与迁移混批）。

---

## 评审结论（2026-09-19，意见已回写正文为 v2）

三路独立核查: 代码事实复核（@explorer）/ Qt·CMake 外部断言（@librarian，含 Qt 官方文档与 Qt 邮件列表佐证）/ 架构评审（@oracle）。检查单逐条结论:

1. **§A.1 崩溃语义**: 接受，附条件——补崩溃转储（`SetUnhandledExceptionFilter` + `MiniDumpWriteDump`，已加入 A.1 表，只转储不恢复）。
2. **§A.3 阶段划分**: 合适；判据②量化为"无 breaking 变更 ≥3 个月"，新增判据④（接受能力门控为建议性）。
3. **§B.3 接口**: IInstanceApi 维持只读；`subscribeRoster` 的 `std::function` → `IRosterObserver` 已修正（原签名违反本节自身 ABI 纪律），可进 P6a 冻结。
4. **§B.1 库粒度**: api+core 足够且恰好，再拆是预付抽象；qml module backing target 归属 P2 开工前定（已写入 P2）。
5. **§C.1 入口**: 满足；IDE 原生读 presets 为净赚。
6. **§C.2 路径硬编码**: 适配；机器差异覆盖首选 `QT_MINGW_ROOT` 环境变量（已明示于 C.2）。
7. **§D 顺序**: 不对调，维持 P1→P2——双构建入口等价性维护税大于提前见插件能力的收益。
8. **§C.5 工具链合并**: 采纳，P1 稳定后独立批次执行；补充代价"renderer 耦合 Qt 发行节奏"已写入 C.5。

**v2 修订摘要**: 事实修正 F1（17 个 context property）/F3（poc 目标）/F7·F11（141 处 SOURCE_DIR、qputenv 脚注）；接口 B.3 观察者反转；A.2 qt_version/build_type 规则与理由修正（对齐 Qt 官方 BC 策略与 MinGW 实情）；A.1 崩溃转储 + CI 诚实度；B.4 能力门控降为建议性表述；B.2 析构假设标注 + 退出 QA；B.6 资源布局（qrc）+ 校验措辞降级；P1a 验收门改产物等价（DLL 清单级）；新增 P1d 最小 CI；估时更新（总量 30–42，MVP 24–30）。

**2026-09-19 三项拍板（已回写正文）**: ① P1d 最小 CI 采纳，计入总量（31–43.5 人日）；② poc 目标随 P2 退役删除（删目标 + `src/poc_main.cpp`；P1a/P1b 期间照常构建并纳入产物清单）；③ qml module backing target 归 pet_panel_core（core 接受 Qt6::Quick 依赖，注册点搬迁变库内搬家）。
