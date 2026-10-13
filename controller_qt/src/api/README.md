# Desktop Pet Panel Plugin SDK (`src/api/`)

第一方插件 SDK 接口头集合（P4 落地，P6a 冻结点前允许演进，冻结后变更走版本化）。
设计合同: `docs/refactor/plugin-architecture-and-cmake-migration.md` §A/§B.3。
**当前版本: API 1.10**（2026-10-13，见文末「版本历史」）。

## 组成

| 头文件 | 内容 |
|---|---|
| `PluginTypes.hpp` | POD/Qt 值类型（InstanceInfo / PackInfo / DownloadRequest / InstallSpec / PageDescriptor / v1.2: InstanceSpec / InstanceRuntime / v1.3: ModelSummary / **v1.4: MonitorSample / MonitorSubscription** / **v1.8: LayoutInfo + MonitorSample.rendererFps 尾部追加** / **v1.10: InstanceSpec.graphicsBackend 尾部追加**）+ PluginError / PluginLogLevel / 宿主 API 版本常量 + 能力词表常量（v1.1: `kCapabilityNetwork` / `kCapabilityVoicepacksInstall` / `kCapabilityInstanceLifecycle` / `kCapabilityInstanceTuning` / `kCapabilitySettingsWrite`，拼写与 manifest 解析逐字一致） |
| `IPanelPlugin.hpp` | 插件唯一入口：`initialize(IPluginContext&)` / `shutdown()`；iid `org.desktop-pet.PanelPlugin/1.0` |
| `IPluginContext.hpp` | 宿主服务面：instanceApi / voicePackApi / uiApi / downloadApi / pluginConfigDir / log + **v1.1 尾部追加 `queryApi`（阶段 2 后新接口族的唯一暴露通道）**；内含 `IExtApi` 标记接口 |
| `IInstanceApi.hpp` | 只读实例花名册 + `IRosterObserver` 纯虚订阅（**禁 std::function**）。只读 = 接口形状保持（v1.0 vtable 布局不动）；「插件永不写实例」的只读**承诺**已被 2026-09-21 决策推翻，写能力走 S2 `IInstanceControlApi`（`queryApi` 暴露，`instance_lifecycle`/`instance_tuning` 能力门控）。v1.2 追加 `IInstanceObserver` + `subscribeInstances`/`unsubscribeInstances` 尾追（实例级状态观察） |
| `IInstanceControlApi.hpp` | **v1.2 (S2)** 实例生命周期写族: create / remove / start / stop / restart / loadModel。继承 `IExtApi` 经 `queryApi` 获取；`instance_lifecycle` 能力 + 宿主 `plugin_write_enabled` 开关门控，未授权为 stub；同步返回=受理，结果经 `IInstanceObserver` |
| `ITuningApi.hpp` | **v1.3 (S5)** 实例调参写族: setOpacity / setVolume / setMuted / setFps / playMotion / setExpression / triggerHitArea / mountVoicePack / unmountVoicePack / **v1.10: setGraphicsBackend**（渲染后端选择器，"opengl"/"vulkan" 大小写不敏感、离线持久化、下次启动生效）。继承 `IExtApi` 经 `queryApi` 获取；`instance_tuning` 能力 + 宿主开关门控；mount 的 packId 是**目录名**（宿主解析为绝对路径，未知包→NotFound） |
| `IModelApi.hpp` | **v1.3 (S5)** 模型库只读族: availableModels / refreshScan / modelInfo（ModelSummary）。经 `queryApi` 获取；**只读不门控**（不受能力与写开关约束），未接线→nullptr |
| `ISettingsApi.hpp` | **v1.3 (S6)** 面板设置写族（类型化方法，禁 QJsonObject 逃生舱）: setCloseAction / setConfirmOnExit / setStartMinimized / setDefaultModelName / **v1.10: setDefaultGraphicsBackend**（新建实例默认渲染后端，"opengl"/"vulkan" 大小写不敏感归一小写）。`settings_write` 能力 + 宿主开关门控；**刻意不含 autoLaunchSystem**（宿主外壳能力不进插件 API） |
| `IMonitorApi.hpp` | **v1.4 (S7)** 资源监控只读族: history（60 样本环形全量，POD 投影 -1 哨兵）+ subscribe/unsubscribe（IMonitorObserver 逐样本推送，每订阅者独立 minIntervalMs 合并节流）。经 `queryApi` 获取；**只读不门控**，未接线→nullptr；宿主自己的 MonitorPage 保留活对象绑定（架构边界），本族面向插件消费者 |
| `IEventApi.hpp` | **v1.5 (S8a)** 事件总线族: Event/EventPriority/IEventListener + subscribe/unsubscribe/publish（**同步分发**——publish 返回时整链已跑完、cancelled 已终值）。经 `queryApi` 获取；**无能力门控、不受写开关**（通知通道而非宿主状态写），未接线→nullptr；插件拿到的是 per-context 代理（发布强制 `plugin.<ownId>.*` 前缀 + publisherId 盖戳，防伪造）；重入深度上限 8（超限 Busy）；宿主通道 ID 词表 + 载荷字段表冻结在 `EventIds.hpp`（目录即契约：字段只加不删不改名，可选值用 -1/"" 哨兵，事件 ID 永不改名移除）；**v1.7 (S9) 目录扩至 21 通道**（+6 `pet.instance.*`、+3 `pet.panel.*` 面板级首启；`kAllEventIds` 完整性数组 + 守卫测试根治「常量加了、词表漏配」缺口）；**v1.8 目录扩至 23 通道**（+`pet.instance.expression_applied` / `pet.instance.motion_progress` 渲染器事件桥接）；**v1.9 目录扩至 26 通道**（+`pet.panel.window_state` / `pet.panel.tray_activated` 面板级窗口/托盘可观察性 + `pet.instance.focus_changed`〔WS `window_focus` 桥接〕，autoLaunchSystem 翻转并入 `settings_changed` 字段集） |
| `IRuleApi.hpp` | **v1.6 (S8f)** 规则注册族: registerRule/unregisterRule（规则 JSON schema 见 `docs/rules.md`）。经 `queryApi` 获取；**注册是惰性动作——无能力词、queryApi 时不查写开关**，owner 门控在**动作发射时活查**（插件规则的 play_motion/set_expression 现查 owner 的 `instance_tuning` 能力 + `plugin_write_enabled`，滞留规则面无法超越其授权存活；notify_bubble/play_pack_audio/publish_event 按设计不门控）；插件拿到的是 per-context 代理（存储 id 强制 `plugin.<ownId>.` 前缀 + `RuleScope::Plugin` 盖戳，只能注册/注销/替换自己的）；schema 校验失败→InvalidArgument（原因经 context log）引擎不变；未接线→nullptr；规则不持久化（随注册存活，卸载清扫兜底） |
| `ILayoutApi.hpp` | **v1.8** 布局族: getLayout（**配置快照**读——InstanceConfig 窗口几何 + 布局缩放投影为 `LayoutInfo`，非实时；实时经 `pet.instance.layout_changed`/`window_resized` 事件观察）+ resetLayout / setAutoStart（受理语义，转调既有 InstanceSession 面）。`instance_tuning` 能力 + 宿主开关门控（照 `pet.instance_tuning` 写族矩阵），未授权/未接线→stub；宿主详情页的自动启动开关与重置布局按钮已换绑到本族（门禁允许清单对应条目随之删除） |
| `IVoicePackApi.hpp` | 语音包发现（listPacks / refreshScan / installPath）+ **v1.3 追加 `IPackListObserver` + subscribePackList/unsubscribePackList**（包列表变化观察；挂载写走 ITuningApi） |
| `IDownloadApi.hpp` | 宿主下载咽喉点（start / cancel / installArchive + DownloadListener 纯虚回调）；manifest 未授 `network` capability 时为 stub（§B.4） |
| `IUiApi.hpp` | 页面注册 + 气泡通知；**无 unregisterPage/页面生命周期（刻意 YAGNI，防 P6a 重议）** |

## ABI 纪律（review 强制；§B.3）

跨界**仅允许**: 纯虚接口、Qt 值类型（QString/QVector/QJsonObject）、POD、`const&` 或值返回。

跨界**禁止**:
1. **`std::` 任何类型**——MinGW 跨 DLL 的 std ABI 陷阱（不同 GCC 版本 libstdc++ 不互通）；
2. **导出模板**；
3. **接口头内 inline 实现**（防跨 DLL ODR/导出问题）。唯一豁免: 各接口的
   `virtual ~I() = default;`（无逻辑、阶段 1 无跨模块符号；阶段 2 前在 P6a 复审
   所有权/删除语义）。

vtable 稳定来自"不改/不重排虚函数声明"。冻结后变更规则（双轨）:

- **通用**: 一切变更升版本；新增枚举值（底类型已固定为 `unsigned int`，加值
  不破坏布局）与全新接口/全新 PluginTypes 类型 = 加法，升 `kApiMinor`；iid
  后缀跟随 `kApiMajor`（major bump → `org.desktop-pet.PanelPlugin/2.0` 新
  iid，旧插件被 api_version 门拒绝）。
- **阶段 1**（同仓同建）: 可向既有接口尾部追加虚方法（升 minor，全量重编消化）。
  **该特权已于 v1.1（2026-09-21）使用一次**——`IPluginContext::queryApi()`
  尾部追加。这是一次性买断: 阶段 2 后一切新接口族经 `queryApi` 暴露，不再需要
  （也不允许）新的尾部追加。
- **阶段 2**（二进制插件）: 既有接口 vtable 与 PluginTypes 结构体**冻结**——
  追加虚方法/加字段 = major 破坏；扩展一律走"新接口（新 iid，继承 `IExtApi`
  标记）经 `IPluginContext::queryApi()` 查询获取"或新类型。v1.1 前"经
  IPluginContext 新 accessor 暴露"的旧表述已被 queryApi 通道取代（否则
  "加 accessor"本身就是 vtable 追加，构成冻结悖论）。

### ABI 门控（§A.2）

动态阶段（阶段 2）`Q_PLUGIN_METADATA` 携带 `abi` 块
（compiler / compiler_version / qt_version / qt_build / build_type），
`PluginRegistry` 在实例化前校验: compiler+version 严格相等；qt 同 major 下
plugin minor ≤ host minor；build_type 严格相等；api_version 宿主 ≥ 插件。
**编译期（阶段 1）插件豁免 abi 校验**——同仓同次构建，字段由 CMake 生成注入，
不手写（见 `plugins/sdk-template/`）。

## 崩溃语义（§A.1——写明，不假装）

**第一方插件 = 面板组成部分。插件崩溃 = 面板崩溃，无运行时隔离。**

- 不做 SEH/信号处理式"隔离"（MinGW 无 `__try`；栈坏时信号处理不安全——假安全
  比没有更危险）。
- 宿主在 IPluginContext 调用边界 **catch 所有异常**（能拦的解析/逻辑错误全拦，
  失败标记 Failed 不毒化后续插件）；拦不住的（内存损坏/死锁）诚实接受，崩溃即
  退出，Windows 侧由 MiniDump 转储兜底诊断。
- 真正的防线是**结构性收窄**: 危险能力 100% 宿主化（下载/解压/落盘只在宿主受测
  代码，§B.4）+ 插件代码量最小化。
- 流程性补偿: 同仓、同 review、ctest/CI 门禁。

## 线程契约（§B.3 生命周期）

- **所有回调都在 GUI 线程**（roster/下载进度/气泡）。插件回调内**禁阻塞**——
  慢回调直接拖慢整个面板事件循环。
- `initialize` ~2s 内返回；`shutdown` 参与宿主全体插件 ≤200ms 总预算，禁重活。
- 插件不得自起线程调用本 SDK（阶段 2 前若确需后台线程，仅限插件内部计算，
  结果须经 GUI 线程转发回 SDK 调用）。

## 能力与网络（§B.4，v2 诚实度）

manifest `capabilities: ["network", ...]` 决定 `downloadApi()` 返回真实现还是
capability stub（每个方法返回 `PluginError::Capability`，响亮失败）。能力字符串
的**唯一权威拼写**是 `PluginTypes.hpp` 的 `kCapability*` 常量（v1.1 起收编，
与 PluginRegistry 的逐字解析、宿主门控的比对大小写敏感一致；未知能力词照单
全收但不门控任何接口）。**对第三方进程内插件这是建议性约束而非执行机制**
（C++ 可自开 socket）——能力清单的价值是审计、用户知情与第一方吊销开关。
硬隔离需回到进程边界（方案 C，未采纳）。

词表与门控范围（v1.1）:

| 常量 | 字符串 | 门控 |
|---|---|---|
| `kCapabilityNetwork` | `network` | IDownloadApi 真实现（P5 起被强制） |
| `kCapabilityVoicepacksInstall` | `voicepacks_install` | （S2）语音包安装管线写入口 |
| `kCapabilityInstanceLifecycle` | `instance_lifecycle` | (S2 已落地 v1.2) IInstanceControlApi 生命周期: 建/删/启/停/重启/切模型 |
| `kCapabilityInstanceTuning` | `instance_tuning` | (S5 已落地 v1.3) ITuningApi 调参: 透明度/音量/静音/帧率/动作/表情/命中/语音包挂载 |
| `kCapabilitySettingsWrite` | `settings_write` | (S6 已落地 v1.3) ISettingsApi 白名单字段写入 |

写开关说明（v1.2 引入、v1.3 扩面）: 宿主 kv `plugin_write_enabled`（缺省开）同时门控
`instance_lifecycle` / `instance_tuning` / `settings_write` 三族——活读（每次 queryApi 现查），
翻转自**下一次 queryApi 调用**起生效: 后续查询拿到 capability stub（方法返回
`PluginError::Capability`），**插件此前已缓存持有的原接口指针不受影响**——字面
"翻转即吊销"需 per-call 门控代理（见"阶段路线"的阶段 2 候选）。只读族（`pet.model`、
`pet.monitor`、voicePackApi、instanceApi）不受其约束。

族 × 能力 × 开关总表（v1.10 全景，queryApi 通道八族——v1.10 零新族，仅既有族加法扩员）:

| apiId | 接口 | 能力门控 | 写开关 | 未接线时 |
|---|---|---|---|---|
| `pet.instance_control` | IInstanceControlApi | `instance_lifecycle` | 受控 | capability stub |
| `pet.instance_tuning` | ITuningApi | `instance_tuning` | 受控 | capability stub |
| `pet.model` | IModelApi | 无（只读） | 不受控 | nullptr |
| `pet.settings` | ISettingsApi | `settings_write` | 受控 | capability stub |
| `pet.monitor` | IMonitorApi | 无（只读） | 不受控 | nullptr |
| `pet.events` | IEventApi | 无（通知通道） | 不受控 | nullptr |
| `pet.rules` | IRuleApi | 无（注册惰性） | 不受控（发射时活查） | nullptr |
| `pet.layout` | ILayoutApi | `instance_tuning` | 受控 | capability stub |

`pet.rules` 的门控列语义与其它写族不同（S8f 设计裁决）: 注册/查询时不门控——门控发生在
**动作发射时**，规则引擎对插件规则的 play_motion/set_expression 每次发射现查 owner 的
`instance_tuning` 能力（PluginRegistry 前缀匹配，未知 owner fail-closed）**且**
`plugin_write_enabled` 为真；任一不满足该动作失败 WARN（命中仍记账）。这比 queryApi 时点
门控更强: 已注册的滞留规则无法在授权被撤销后继续起效，也不存在"缓存指针绕过"面。

另有三个经 IPluginContext 固定 accessor 暴露的 v1.0 族: instanceApi / voicePackApi /
uiApi / downloadApi（downloadApi 由 `network` 能力门控真实现 vs stub）。

### QJsonObject 载荷纪律（v1.5，pet.events）

`pet::Event.payload` 是本 SDK 唯一自由形状的 QJsonObject 载荷。纪律（冻结于
`EventIds.hpp` 头注释 + 各事件字段表）:

- **snake_case 键**，与协议/配置层一致；
- **字段只加不删不改名**——消费者必须忽略未知键；
- **可选值用哨兵不用缺键**: 数值 -1、字符串 ""（禁 `std::optional` 跨界，与
  MonitorSample 的 -1 哨兵同一纪律）;
- **宿主事件 ID 词表冻结在 `api/EventIds.hpp`**（`pet.instance.*` /
  `pet.model.*` / `pet.pack.*` 三域，"一域一通道"）; 事件 ID 永不改名移除;
- **插件只可发 `plugin.<ownId>.*`**（per-context 代理强制 + publisherId 盖戳;
  宿主可发任意合法 ID）;
- 键名拼写由 `core/EventCatalog.hpp` 的 typed builder 单点拥有——宿主发射点
  禁止手写键名（与 Protocol 命令工厂同一纪律）。

## 配置写规约（v2）

`pluginConfigDir()` 返回插件私有可写区。写入规约与面板自身一致: **JSON +
QSaveFile 原子写 + snake_case 键**——禁止裸 QFile::write 持久化（项目反模式）。

## 阶段路线（§A.3）

- **阶段 1（当前）**: 编译期静态插件——`plugins/<name>/` 源码目录 → STATIC 库链进
  exe，CMake 生成工厂表（`static_plugins.cpp`），同一次编译，ABI 风险为零。
- **阶段 2（开启判据见 §A.3 四条）**: 独立 dll + QPluginLoader + abi 门控。接口
  已按阶段 2 标准设计，届时"换加载器"而非"改接口"。
- **阶段 2 所有权前提（P6a 评审结论）**: 当前"宿主 create / 宿主 delete"（含
  defaulted 虚析构豁免）仅对阶段 1 同二进制成立；开启阶段 2 的附加判据（第 5
  条）= 工厂接口增加 `destroy(IPanelPlugin*)` 并由 PluginHost 切换为插件模块
  内销毁（跨模块 delete 是 UB）。listener/observer 归插件所有、宿主只调不删
  的契约两阶段通用。
- **阶段 2 候选（可选）**: per-call 门控代理——每个写族一个转发类（持真实现
  指针 + 每次调用内现查 `plugin_write_enabled`，关闭即返回
  `PluginError::Capability`），可兑现字面"翻转即吊销"承诺（插件已缓存的旧
  指针同样被拒），弥合现行"下次 queryApi 起生效、缓存指针不受影响"语义。

## 模板

`plugins/sdk-template/` 是活着的最小插件（示例页面 + manifest + CMake），拷贝
即得正确工具链与布局约定。第三方面向 `IUiApi.registerPage` 的 qmlUrl 一律使用
`qrc:/` URL（§B.6）。

## 版本历史

### v1.10（2026-10-13，kApiMajor=1 / kApiMinor=10）— 渲染后端选择器与面板默认后端

全部为**加法**变更（两个既有写族的尾部追加 + InstanceSpec 尾部成员 + 事件字段集
扩员 + 宿主写面接线），零既有签名/顺序改动；`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 9 → 10**。
2. **`ITuningApi` 第 10 个调参操作 `setGraphicsBackend(uuid, backend)`**（族版本
   不变，尾部追加）: 渲染后端选择器，合法值恰为 `"opengl"` / `"vulkan"`
   （大小写不敏感，宿主归一为小写；其它值含空串 → InvalidArgument，无静默纠偏）。
   **离线-only**: 持久化 `InstanceConfig.graphics_backend`、下次实例启动生效
   （不同后端 = 不同渲染器可执行文件，刻意不发在线命令——运行中的渲染器维持
   其后端直至重启）。NotFound/Busy 照 locateAlive 先例。
3. **`ISettingsApi` 第 5 个白名单写 `setDefaultGraphicsBackend(backend)`**（族
   版本不变，尾部追加）: 新建实例的默认渲染后端（`PanelConfig.
   default_graphics_backend`，缺省 `"opengl"`）。同一验证词表（"opengl"/"vulkan"
   大小写不敏感归一小写，否则 InvalidArgument）；成功持久化后发射
   `pet.panel.settings_changed` `{field: "default_graphics_backend", value}`
   （v1.7 通道字段集 add-only 扩员，零新事件 ID）。
4. **`InstanceSpec.graphicsBackend` 尾部追加**（create() 的后端覆盖）: 空串 =
   InstanceConfig 缺省（"opengl"）；非空须合法（大小写不敏感归一小写），否则
   create 在**铸造 uuid 之前**返回 InvalidArgument。
5. **宿主接线**: InstanceSession 增 READ-only `graphicsBackend` Q_PROPERTY +
   `setGraphicsBackend` Q_INVOKABLE（离线持久化 setter，照 loadModel 离线半边
   纪律: 同值 no-op、恒持久化、发 NOTIFY、零命令）+ `graphicsBackendChanged`;
   TuningApiImpl/SettingsApiImpl 真实现 + TuningApiStub/SettingsApiStub 各补
   Capability 桩; InstanceManager::createInstance 尾追可选 `graphicsBackend` 参数;
   InstanceControlBridge 增 `setGraphicsBackend`（int 错误码，照 setFps 镜像）;
   RosterApiModel.createInstance 尾追默认空串第 5 参（旧 QML 调用点不变）;
   PanelConfigController 增 `defaultGraphicsBackend` Q_PROPERTY（settingsApi 转发
   + 词表守卫 + 空值回退直写路径）; `api_boundary_gate.sh` QML_UNIQUE 模式表补
   `setGraphicsBackend` + 自咬探针（模式表维护规约）。
6. **测试**: TuningApiImplTest 增 testGraphicsBackendWrite（round-trip/归一/
   非法/NotFound/Busy/同值）; SettingsApiImplTest round-trip 改五字段 +
   testDefaultGraphicsBackendValidation; EventBusEmissionTest settings_changed
   断言 4 → 5 发; PanelConfigTest 键数 15 → 16 + 控制器桥 round-trip;
   PluginQueryApiTest 桩 Capability × 2; InstanceControlApiImplTest create
   三分支（VULKAN 归一/空=缺省/非法拒绝）; RosterApiModelTest 第 5 参透传。

### v1.9（2026-10-13，kApiMajor=1 / kApiMinor=9）— 边界事件补全（窗口/托盘/焦点 + autoLaunch 纳入，目录 23 → 26）

全部为**加法**变更（3 条新事件通道 + 既有通道字段集扩员 + 发射点旁路追加），零既有
签名/顺序改动；`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 8 → 9**。
2. **三组新边界事件**（`docs/bus-events.md` §6.24-§6.26 字段表）:
   - **`pet.panel.window_state`** `{state, visible}`——面板主窗口可见性/状态变迁
     （面板级）。事件源 = `core::PanelWindowObserver`（新文件，PanelUiBoot 在引擎
     `objectCreated` 时挂接 root window 的 `QWindow::visibilityChanged`——post-commit
     单点）。state 映射: Windowed→`normal` / Maximized→`maximized` / Minimized→
     `minimized` / **Hidden→`hidden`（隐藏到托盘语义，closeAction "minimize" 分支与
     托盘 toggle 都走 `root.hide()`）**；FullScreen/`AutomaticVisibility` 为防御性
     兜底（本 UI 不可达）。cancellable=false。
   - **`pet.panel.tray_activated`** `{action}`——托盘交互（面板级）。事件源 =
     TrayManager 既有 4 信号位（发射点在 TrayManager 内部——托盘手势与 QML 菜单项
     同路扇出，改动最小且直接可测）。映射: `visibilityToggled`→`toggle_visibility`、
     `showSettings`→`settings`、`quitRequested`→`quit`、`requestContextMenu`→
     `context_menu`。cancellable=false。
   - **`pet.instance.focus_changed`** `{focused}`（verbatim）——渲染器 WS
     `window_focus`（events.md §18）桥接。EventRegistry `kPhase6Events` 注册默认
     DEBUG 日志 + InstanceSession publish-only override（`expression_applied` 模式，
     复现默认日志）。cancellable=false。
3. **autoLaunchSystem 翻转纳入 `pet.panel.settings_changed`**（v1.9 改口，零新
   ID）: `PanelConfigController` 照 `SettingsApiImpl` 先例加 `setEventPublisher`
   纯 C++ seam（非 Q_INVOKABLE/WRITE——门禁零维护），`setAutoLaunchSystem` 成功
   路径（持久化之后；AutoLaunchManager 动作由 QML 在属性写入前驱动）发射
   `{field: "auto_launch_system", value: <bool>}`。写路径本身仍刻意不经
   ISettingsApi（宿主壳能力裁决不变）；失败回滚与同值 no-op 不发。
4. **目录同步**: `kAllEventIds` 23 → 26、`makeKnownEventTable` 同步（守卫测试
   `testKnownEventTableCoversCatalog` 双向强制 26 条）、EventCatalog 增
   `makeWindowStatePayload` / `makeTrayActivatedPayload` /
   `makeFocusChangedPayload` 三个 builder（键名单点）。
5. **测试**: `EventBusEmissionTest` 增 autoLaunch（成功发/失败不发/同值不发）+
   focus_changed verbatim 双值透传两槽；`TrayManagerTest` 增
   `testTrayActivatedEmission`（4 action + 忽略路径 + null publisher）；新
   `PanelWindowObserverTest`（全枚举映射表 + offscreen QWindow 真发射 + 降级
   装配）。基线 65 → 66 全绿，门禁 clean。

### v1.8（2026-10-13，kApiMajor=1 / kApiMinor=8）— 布局族 + 渲染器事件桥接 + fps 进监控快照

全部为**加法**变更（一个经 queryApi 暴露的新布局族 + MonitorSample 尾部追加 + 两条渲染器事件的
面板侧桥接 + 宿主写换绑收口），零既有签名/顺序改动；`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 7 → 8**。
2. **`ILayoutApi`**（新头，apiId `pet.layout`，族版本 1）+ **`LayoutInfo` POD**: getLayout 语义 =
   **配置快照**（InstanceConfig 持久化半边投影: x=windowX / y=windowY / width=windowWidth /
   height=windowHeight / scale=layoutScale〔set_layout 域，非 modelScale〕；实时变化经
   `pet.instance.layout_changed`/`window_resized` 总线事件观察）+ resetLayout / setAutoStart
   受理语义（转调既有 InstanceSession 面，NotFound/Busy 照 TuningApiImpl 先例）。门控矩阵照
   `pet.instance_tuning` 写族（`instance_tuning` 能力 × `plugin_write_enabled` 活读 × 注入；
   未授权/未接线 → `LayoutApiStub` 全方法 Capability）。宿主装配: LayoutApiImpl 共享单例
   （PanelApplication 服务树）+ PluginHost `setLayoutApi` 注入口/per-host 回退 + queryApi 路由。
3. **`MonitorSample.rendererFps` 尾部追加**: 渲染器 `stats_state` 的 `fps` 键（EMA 实测帧率，
   events.md §14）经 `RendererStats.fps`（恒为 double，-1 哨兵）投影进 MonitorSample **尾部**
   （-1 = 键缺失/无效/首帧前哨兵直传——结构体尾部追加纪律，冻结后仅允许的加法形状）。
4. **两事件桥接**（渲染器已发、面板收）: `kEventInstanceExpressionApplied`（`{expression_id}`，
   events.md §16 逐字——set_expression 成功回执）+ `kEventInstanceMotionProgress`（双变体
   `{group,index,progress?}` / `{motion_path,progress?}`，events.md §17 逐字——500ms 渲染器侧
   节流；`progress` 可选，循环/未解析时省略）。均 cancellable=false、publish-only override
   （audio_finished 模式——复现默认 DEBUG 日志 + EventCatalog builder 透传，motion_progress
   高频特意零额外处理）。`kAllEventIds` 21 → 23，`makeKnownEventTable` 同步（守卫测试强制）。
5. **宿主 dogfooding + 门禁允许清单清理**: `InstanceControlBridge` 增 setAutoStart /
   resetLayout（int 错误码）+ getLayout（QVariantMap `{ok,x,y,width,height,scale}` /
   `{ok:false,error}`），构造注入共享 `pet::ILayoutApi`；InstanceDetailPage 的
   `instance.setAutoStart(`/`instance.resetLayout(` 换绑到 `instanceControl`（读绑定不动）；
   `api_boundary_gate.sh` 允许清单删除对应 2 条（stopAll 与 VoicePackController 测试回退条目
   保留），布局直调自此为普通违规。

### v1.7（2026-10-13，kApiMajor=1 / kApiMinor=7）— S9 事件总线目录补全（9 新通道 + kAllEventIds 完整性守卫 + pet.panel 域启用）

全部为**加法**变更（事件目录扩展 + 发射点旁路追加），零既有签名/顺序改动；
`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 6 → 7**。
2. **`EventIds.hpp` 目录扩至 21 条通道**: 新增 6 条 `pet.instance.*`
   （`kEventInstanceError`/`kEventInstanceDialogue`/
   `kEventInstanceRestartScheduled`/`kEventInstanceGaveUp`/
   `kEventInstanceLayoutChanged`/`kEventInstanceWindowResized`）+
   3 条 `pet.panel.*`（`kEventPanelSettingsChanged`/`kEventPanelPluginLoaded`/
   `kEventPanelPluginUnloaded`——**pet.panel 面板级域首次启用**，
   `instanceUuid = ""`，v1.5 设计预留域）。全部 `cancellable=false`。
3. **`kAllEventIds` 完整性数组**: `EventIds.hpp` 新增 constexpr 数组枚举全部
   21 个 id（注释铁律「新增事件必须同步本数组」）；`makeKnownEventTable`
   补齐到 21 条全量并改为 public static（纯工厂）；完整性守卫测试
   （`EventBusEmissionTest::testKnownEventTableCoversCatalog`）断言表键集 ==
   数组全集（逐 id 比对，双向 + 去重检查）——`audio_finished` 式
   「常量加了、表漏配」缺口从此测试即红（该缺口曾使 `cancel_default`
   规则对合法事件误 WARN）。
4. **发射点**（全部 EventCatalog builder 组装、发射点禁手写键名）:
   - `pet.instance.error`: WS `error` 桥接，publish-only override 复现
     EventRegistry 默认 INFO 日志（`audio_finished` 模式），载荷 §9 逐字；
   - `pet.instance.dialogue`: 会话内 MBE 对话 sink 流入处（uuid+文本+时长
     共存的最早单点），载荷 `{text, duration_ms}`；双通道分工——气泡流仍为
     显示通道，本事件为可观察性通道；
   - `pet.instance.restart_scheduled`/`gave_up`: RestartController retry/
     gaveUp 接线处（`InstanceSession` 构造），载荷 `{attempt, delay_ms}`/
     `{attempts}`（RestartController 新增 `lastArmedDelayMs()` 只读访问子）；
   - `pet.instance.layout_changed`/`window_resized`: 既有持久化 handler 的
     post-commit 追加发射，载荷 §12/§13 逐字（畸形载荷早退不发）；
   - `pet.panel.settings_changed`: `SettingsApiImpl` 四个类型化 setter 成功
     持久化后（`setEventPublisher` 注入 seam，PanelApplication/PluginHost
     回退对均接线），载荷 `{field, value}`；`autoLaunchSystem` 走宿主直路径
     **刻意不发**；
   - `pet.panel.plugin_loaded`/`plugin_unloaded`: `PluginHost::
     initializeAll` 每插件 Started 后 / `shutdownAll` 每插件清扫完成后，
     载荷 `{plugin_id}`。
5. **文档**: `docs/bus-events.md` 9 个新事件各成节 + 通道总数 12 → 21 +
   新增「未收录事件与理由」节（动作进度/实际帧率高频流、`layout_state`
   查询回执、布局命令面 API 缺口→S7 记录债/lifecycle v2 候选）。

### v1.6（2026-10-12，kApiMajor=1 / kApiMinor=6）— S8f 规则引擎插件注册通道 + owner 动作门控

全部为**加法**变更（一个经 queryApi 暴露的新规则注册族 + 宿主侧 owner 门控/清扫接线），
零既有签名/顺序改动；`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 5 → 6**。
2. **`IRuleApi`**（新头，apiId `pet.rules`，族版本 1）: registerRule（原子注册/替换本插件
   一条规则）+ unregisterRule（按裸 id 注销，前缀限定只能删自己的）。规则 JSON 的
   `schema_version` 是**规则文档格式版本**（当前 1），与族版本解耦。
3. **惰性注册 + 发射时 owner 门控**（S8f 设计裁决）: 无能力词、queryApi 时不查写开关；
   插件规则（存储 id 前缀 `plugin.<ownId>.`）发射 play_motion/set_expression 动作时，
   `RuleEngineService` **活查** owner 的 `instance_tuning` 能力（注入的能力查询器，
   PanelApplication 用 PluginRegistry 前缀匹配实现——插件 id 带点，服务侧无法无歧义
   切分；未知 owner fail-closed）**且** `plugin_write_enabled`（注入活读 provider，
   缺省=开）；任一不满足 → 该动作失败 WARN（命中仍记账、fail-fast 同既有语义）。
   notify_bubble/play_pack_audio/publish_event 按设计**不** owner 门控（气泡是通知面、
   pack 音频已有挂载校验、publish_event 是宿主身份 + 编译期前缀校验）。宿主规则永不门控。
4. **身份规则**（per-plugin `RuleApiProxy` 强制）: registerRule 强制
   scope=`RuleScope::Plugin` + ownerUuid=插件 id，存储 id 盖 `plugin.<ownId>.` 前缀
   （同裸 id = 原子替换自己的注册；跨 owner 冲突拒绝 InvalidArgument——嵌套插件 id 场景
   以存储规则的 ownerUuid 复核，不信任 id 前缀本身）；unregisterRule 传**裸 id**，
   代理重新盖前缀后按 ownerUuid 复核再删，只能删到自己的规则；未知 id 合法 no-op。
   schema 校验失败 → InvalidArgument（结构化原因经插件 context log），引擎状态不变；
   超 MAX_RULES(200) 新 id 同样 InvalidArgument（容量内替换既有 id 仍合法）。
5. **宿主装配**: RuleApiImpl 共享单例（PanelApplication 服务树，注册于 RuleEngineService
   之后）+ PluginHost `setRuleApi` 注入口/per-host 回退对（无服务树时降级
   RuleEngineService over 回退总线——启动零装载、运行时注册仍可订阅求值）+ queryApi
   路由（注入→per-context Proxy；未接线→nullptr；版本超→nullptr）。**卸载清扫**:
   `PluginHost::shutdownAll` 在插件 `shutdown()` 之后、delete 之前按
   `removeRulesByOwnerPrefix("plugin.<id>.")` 清扫残留规则（非零 WARN——清扫是安全网
   不是契约，事件订阅清扫同款位置/同款语义）。插件规则**不持久化**（随注册存活，
   面板重启由插件 initialize() 重新注册）。
6. **规则文档**: 全字段表/拒载错误码表（32 项）/条件动作词汇表/触发优先级阶梯/
   插件注册示例/北极星示例 → `docs/rules.md`。

### v1.5（2026-10-12，kApiMajor=1 / kApiMinor=5）— S8a 事件总线（总线核心 + 契约 + 并行旁路发射）

全部为**加法**变更（一个经 queryApi 暴露的新事件总线族 + 两个契约头），零既有签名/
顺序改动；`IPluginContext` 尾追特权未使用。本阶段铁律: **零行为变化**——旁路发射，
无人兑现 `event.cancelled`，既有管道一行不改（只允许"追加发射"）。

1. **`kApiMinor` 4 → 5**。
2. **`IEventApi`**（新头，apiId `pet.events`，族版本 1）: `Event`（eventId/
   instanceUuid/publisherId/timestampMs/cancellable/cancelled/payload）+
   `EventPriority`（Highest=0 … Lowest=4，底类型 `unsigned int`）+
   `IEventListener`（GUI 线程快进快出）+ subscribe/unsubscribe/publish。
   **同步分发语义**: publish 返回时整链已分发、cancelled 已终值。订阅未知
   eventId 合法（先订后发）; 重复 (eventId,listener) 订阅=合并更新优先级;
   shutdown 前必须 unsubscribe 全部（宿主卸载清扫兜底，漏退订 WARN 不悬垂）;
   监听器生命周期归插件。**防伪造前缀规则**: 插件仅可发 `plugin.<ownId>.*`
   （代理违规→InvalidArgument 零分发），宿主可发任意。重入深度上限 8
   （per-thread，超限 Busy）。
3. **`EventIds.hpp`**（新头）: 宿主事件 ID 常量（`kEventInstanceReady/Hit/
   MotionStarted/MotionFinished/IdleTick`、`kEventDragStarted/DragEnded`、
   `kEventModelLoaded/LoadFailed`、`kEventPackMounted/Unmounted`，`pet.instance.*`/
   `pet.model.*`/`pet.pack.*` 三域，"一域一通道"）+ 每事件 payload 字段表
   （字段/类型/语义/哨兵，照 events.md 风格）+ ID 语法规则常量
   （≥3 段、非空段、仅 [a-z0-9_]、拒通配符）。**目录即契约**: 字段只加不删
   不改名、可选字段用哨兵（-1/""）、事件 ID 永不改名移除。
4. **载荷纪律**: QJsonObject snake_case 键; 键名由 `core/EventCatalog.hpp`
   typed builder 单点拥有（hit 键与 events.md §hit 逐字一致; motion_finished
   双变体归一 {group,index(-1 哨兵),motion_path("" 哨兵)}; model_loaded 的
   motion_groups/expressions 由 ModelInfoParser 补全——宿主增补字段）。
   **宿主发射点禁止手写键名**。
5. **门控矩阵**: `pet.events` 无能力门控、不受 `plugin_write_enabled` 写开关
   （开关撤销的是宿主状态写; 总线是通知通道，插件侧伪造防护=代理前缀规则）。
   注入即返回 per-context 代理（`EventApiProxy`: publish 强制
   publisherId=插件 id + 前缀校验）; 未接线→nullptr（照 pet.model 读族先例）。
   宿主桥（发射点）持裸共享 `EventApiImpl`——宿主可发 `pet.*`。
6. **宿主装配**: `core::EventBus`（订阅表/快照扇出/逐监听器异常隔离/重入上限/
   publisherId 记账 + `unsubscribeByPublisher` 卸载清扫）+ `EventApiImpl`
   共享单例（PanelApplication 服务树，**注册于 InstanceManager 之前**——会话
   持发布者指针，总线必须后于所有会话析构）+ PluginHost 注入口/per-host 回退 +
   queryApi 路由。PluginHost 卸载路径调 `unsubscribeByPublisher`（插件
   shutdown 后、delete 实例前）。
7. **旁路发射点**（全部追加、既有逻辑一行不动）: InstanceSession 各 WS 事件
   handler（hit 可取消、ready/model_loaded/drag_end/model_load_failed 提交后、
   motion_started/drag_start 保默认日志的 publish-only override）、Scheduler
   idle 回调（idle_tick 可取消，S8a 不读 cancelled，TODO(S8b)）、语音包
   挂载/卸载（与 mountedVoicePackChanged NOTIFY 同点同 gate）。注入形式:
   `InstanceSession::setEventPublisher(pet::IEventApi*)` 纯 C++ setter
   （非 Q_INVOKABLE/非 WRITE 属性——门禁模式表零维护，setDialogueSink 同款），
   PanelApplication 对存量+rowsInserted 新建会话统一注入。
8. **勘误（2026-10-12 审计补记）**: `kEventModelLoaded`/`kEventModelLoadFailed`
   两事件 ID 实施为 `pet.model.loaded`/`pet.model.load_failed`——设计目录原写
   `pet.instance.model_*`，实施时取与 `pet.model` API 族对称的 `pet.model.*`
   新域命名。ID 已随 v1.5 冻结（目录即契约），以实施为准、不再回改。

### v1.4（2026-09-21，kApiMajor=1 / kApiMinor=4）— S7 监控流 + 边界门禁

全部为**加法**变更（一个经 queryApi 暴露的新只读族 + 两个 POD），零既有签名/顺序
改动；`IPluginContext` 尾追特权未使用。

1. **`kApiMinor` 3 → 4**。
2. **`MonitorSample` / `MonitorSubscription` POD**（`PluginTypes.hpp`）:
   扁平监控样本（controller/renderer CPU、RSS；GPU/VRAM **-1 哨兵**表示未上报——
   禁 `std::optional` 跨界，投影规则与 MonitorPage 的 -1 显示哨兵一致）+
   订阅节流参数（`minIntervalMs` 默认 2000 = 宿主 2s 轮询节奏的合并下限）。
3. **`IMonitorApi`**（新头，apiId `pet.monitor`，族版本 1）: history（60 样本
   环形全量，未知 uuid→空，绝不报错）+ subscribe/unsubscribe（`IMonitorObserver`
   逐样本推送）。只读族——**不受能力与写开关门控**，注入即返回，未接线→nullptr。
   数据源是每实例 MonitorDataModel（与 Monitor 页同一环形缓冲，零模型改动）；
   节流为**每订阅者独立合并**（窗口内样本跳过、不排队，下次推送带最新快照）。
   **宿主边界**: 面板自己的 MonitorPage/QML 保留 `session.monitorModel()` 活对象
   绑定（读留活对象）；本族面向插件消费者，图表序列组装是订阅者自己的职责。
4. **宿主装配**: MonitorApiImpl 共享单例（PanelApplication 服务树）+ PluginHost
   注入口/per-host 回退 + queryApi 路由（照 pet.model 只读族模式）。
5. **API 边界门禁**（R3 落地）: `controller_qt/scripts/api_boundary_gate.sh`
   ——grep 检测 QML（`qml/**/*.qml`）与宿主桥（`src/ui/*.cpp`）对核心写方法的
   直调（instanceControl/rosterModel 桥接收者豁免；允许清单逐条注释理由）；
   经 ctest（ApiBoundaryGateTest + 自咬 ApiBoundaryGateSelfTest）强制执行。
   随本版换绑收口: ModelLibraryPage 两处 loadModel + playMotion 试播、
   WelcomePage 三处启停/重启、VoicePackPage triggerHitArea 试触发全部改走
   instanceControl 桥（读保留活对象，`model_load_failed` 回滚反馈不动）。

### v1.3（2026-09-21，kApiMajor=1 / kApiMinor=3）— S5 运行面+模型域 / S6 设置+语音包域

全部为**加法**变更（三个经 queryApi 暴露的新接口族 + IVoicePackApi 尾追 + ModelSummary
POD），零既有签名/顺序改动。`IPluginContext` 的 v1.1 一次性尾追特权**未再次使用**——
三族全部经既有 `queryApi()` 通道暴露；IVoicePackApi 的尾追属于阶段 1 未冻结族的常规
演进（P6a 冻结后该族同样只经 queryApi 扩展）。

1. **`kApiMinor` 2 → 3**。
2. **`ModelSummary` POD**（`PluginTypes.hpp`）: IModelApi 的行类型（name +
   motionGroups / expressions / hitAreas）。
3. **`ITuningApi`**（新头，apiId `pet.instance_tuning`，族版本 1）: 九个实例调参操作
   （透明度/音量/静音/帧率/playMotion/setExpression/triggerHitArea/mount/unmount；
   v1.10 尾追 setGraphicsBackend 增至十个）。
   同步返回=受理（Ok/NotFound/InvalidArgument/Busy），值变化经
   `IInstanceObserver.instanceStateChanged` 观察；pending-delete 期间全部 Busy。
   mount 的 packId 为**目录名**，宿主按自身扫描反解为绝对路径，未知→NotFound
   （防任意路径注入）。
4. **`IModelApi`**（新头，apiId `pet.model`，族版本 1）: availableModels /
   refreshScan / modelInfo。只读族——**不受能力与写开关门控**，注入即返回，未接线
   →nullptr。数据源是宿主单一模型扫描缓存（与模型库页面同一份）。
5. **`ISettingsApi`**（新头，apiId `pet.settings`，族版本 1）: 四个类型化白名单写
   （setCloseAction 合法值 exit|minimize、setConfirmOnExit、setStartMinimized、
   setDefaultModelName；v1.10 尾追 setDefaultGraphicsBackend 增至五个）。
   **刻意不含 autoLaunchSystem**（OS 级自启动=宿主外壳能力，
   架构决策）与 plugin_write_enabled 本身（插件可翻的开关不是开关）。
6. **`IVoicePackApi` 尾追 `IPackListObserver`** + `subscribePackList`/
   `unsubscribePackList`: 包列表变化观察（粗粒度 packListChanged），挂载/卸载写路径
   属 ITuningApi。
7. **能力门控落地**: `instance_tuning` 与 `settings_write`（v1.1 预留词）随本版开始
   强制；写开关 `plugin_write_enabled` 活读门控三写族。
8. **宿主侧 dogfooding**: 详情页全部写调用改走 `InstanceControlBridge`（ctx prop
   `instanceControl`，共享 TuningApi/InstanceControlApi，宿主桥不被门控）；创建流改走
   `rosterModel.createInstance`；VoicePackController 挂载/卸载内部转调注入的
   ITuningApi；PanelConfigController 四字段写内部转调注入的 ISettingsApi；
   ModelController/VoicePackController 消费共享 ModelApiImpl / 单一包扫描；
   InstanceSession 增补 `mountedVoicePackChanged` NOTIFY（对 S4 冻结面的最小缝，
   mountedPackId 由"下次属性翻转携带"变为实时）。

### v1.2（2026-09-21，kApiMajor=1 / kApiMinor=2）— S2 插件写路径

全部为**加法**变更（一处阶段 1 尾部追加特权消费 + 一个经 queryApi 暴露的新接口族），
零既有签名/顺序改动。路线图 S2「插件写 API 闭环 + 首个写路径 dogfooding」。

1. **`kApiMinor` 1 → 2**。
2. **`InstanceSpec` / `InstanceRuntime` POD**（`PluginTypes.hpp`）: create 入参与
   实例级状态快照（observer 载荷）。
3. **`IInstanceControlApi`**（新头 `IInstanceControlApi.hpp`，继承 `IExtApi`，
   **经 `queryApi` 暴露**——v1.1 铺好的通道首次投入使用）: create / remove /
   start / stop / restart / loadModel 六个生命周期操作。同步返回 = 受理而非完成
   （Ok/NotFound/InvalidArgument/Busy），最终结果经 `IInstanceObserver` 状态事件；
   **无 per-op 回调**。apiId `pet.instance_control`，族版本 1
   （`kInstanceControlApiId` / `kInstanceControlApiVersion`）。
4. **能力门控**: manifest 授 `instance_lifecycle`
   （`kCapabilityInstanceLifecycle`）且宿主开关 `plugin_write_enabled`（面板
   kv，缺省开）时返回共享真实现；否则返回 capability stub（每个方法
   `PluginError::Capability`）。未知 apiId / minVersion 超过族版本 → nullptr。
5. **`IInstanceObserver` + `IInstanceApi` 尾部追加
   `subscribeInstances`/`unsubscribeInstances`**（阶段 1 尾追特权随本次 minor
   一并消化; 观察语义非写路径）: rosterChanged（与 `IRosterObserver` 一致）+
   `instanceStateChanged(uuid, InstanceRuntime)`（粗粒度全量快照）+
   `modelLoadFailed(uuid, error)`。
6. **宿主侧 dogfooding**: 面板侧边栏的实例删除写路径改走
   `RosterApiModel`（内部转调注入的 `pet::IInstanceControlApi`，宿主桥**不经
   context、不被能力门控**——设计决策: 宿主 UI 即宿主，S5 再迁移其余页面）。

### v1.1（2026-09-21，kApiMajor=1 / kApiMinor=1）

全部为**加法**变更，零既有签名/顺序改动；除 queryApi 桩外零运行时行为变化。
决策记录: `docs/refactor/plugin-architecture-and-cmake-migration.md` 修订 v3。

1. **`kApiMinor` 0 → 1**（minor bump 消化全部加法）。
2. **能力词表常量**（`PluginTypes.hpp` `kCapability*` × 5）: `network`
   （既有拼写收编为常量）+ S2 预留 `voicepacks_install` / `instance_lifecycle`
   / `instance_tuning` / `settings_write`，附各自门控的接口族注释。
3. **`IExtApi` 标记接口 + `IPluginContext::queryApi(apiId, minVersion)` 尾部
   追加**: 阶段 2 后新接口族的唯一暴露通道（未知 apiId / 版本不足 →
   nullptr）。解决"新 accessor 本身就是 vtable 追加"的冻结悖论——阶段 1 尾部
   追加特权就此一次性用掉。宿主实现（PluginContextImpl）当前一律返回
   nullptr，S2 起按接口族接入。
4. **推翻 IInstanceApi 只读承诺**: 2026-09-19 P6a 评审"维持只读"结论由
   2026-09-21 架构决策推翻（理由: 面板 dogfooding + 插件写能力；第一方插件
   本就是面板组成部分，收权不增安全）。接口本身保持只读形状（v1.0 vtable
   冻结不动）；写能力落 S2 新接口族 `IInstanceControlApi`（经 queryApi 暴露，
   `instance_lifecycle` / `instance_tuning` 门控）。
5. 计划（S2 配套，未落地）: 宿主全局写能力吊销开关 `plugin_write_enabled`。

### v1.0（2026-09-19，P6a 冻结点）

初始冻结版本（提交 b446040，报告见 `baselines/p6a-freeze-2026-09-19.md`）。
7 个头文件: IPanelPlugin / IPluginContext / IInstanceApi（含 IRosterObserver）/
IVoicePackApi / IDownloadApi（含 DownloadListener）/ IUiApi / PluginTypes。
