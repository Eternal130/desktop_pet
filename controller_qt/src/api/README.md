# Desktop Pet Panel Plugin SDK (`src/api/`)

第一方插件 SDK 接口头集合（P4 落地，P6a 冻结点前允许演进，冻结后变更走版本化）。
设计合同: `docs/refactor/plugin-architecture-and-cmake-migration.md` §A/§B.3。
**当前版本: API 1.2**（2026-09-21，见文末「版本历史」）。

## 组成

| 头文件 | 内容 |
|---|---|
| `PluginTypes.hpp` | POD/Qt 值类型（InstanceInfo / PackInfo / DownloadRequest / InstallSpec / PageDescriptor / **v1.2: InstanceSpec / InstanceRuntime**）+ PluginError / PluginLogLevel / 宿主 API 版本常量 + 能力词表常量（v1.1: `kCapabilityNetwork` / `kCapabilityVoicepacksInstall` / `kCapabilityInstanceLifecycle` / `kCapabilityInstanceTuning` / `kCapabilitySettingsWrite`，拼写与 manifest 解析逐字一致） |
| `IPanelPlugin.hpp` | 插件唯一入口：`initialize(IPluginContext&)` / `shutdown()`；iid `org.desktop-pet.PanelPlugin/1.0` |
| `IPluginContext.hpp` | 宿主服务面：instanceApi / voicePackApi / uiApi / downloadApi / pluginConfigDir / log + **v1.1 尾部追加 `queryApi`（阶段 2 后新接口族的唯一暴露通道）**；内含 `IExtApi` 标记接口 |
| `IInstanceApi.hpp` | 只读实例花名册 + `IRosterObserver` 纯虚订阅（**禁 std::function**）。只读 = 接口形状保持（v1.0 vtable 布局不动）；「插件永不写实例」的只读**承诺**已被 2026-09-21 决策推翻，写能力走 S2 `IInstanceControlApi`（`queryApi` 暴露，`instance_lifecycle`/`instance_tuning` 能力门控）。v1.2 追加 `IInstanceObserver` + `subscribeInstances`/`unsubscribeInstances` 尾追（实例级状态观察） |
| `IInstanceControlApi.hpp` | **v1.2 (S2)** 实例生命周期写族: create / remove / start / stop / restart / loadModel。继承 `IExtApi` 经 `queryApi` 获取；`instance_lifecycle` 能力 + 宿主 `plugin_write_enabled` 开关门控，未授权为 stub；同步返回=受理，结果经 `IInstanceObserver` |
| `IVoicePackApi.hpp` | 语音包发现（listPacks / refreshScan / installPath） |
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
| `kCapabilityNetwork` | `network` | IDownloadApi 真实现（当前唯一被强制的能力，P5） |
| `kCapabilityVoicepacksInstall` | `voicepacks_install` | （S2）语音包安装管线写入口 |
| `kCapabilityInstanceLifecycle` | `instance_lifecycle` | (S2 已落地 v1.2) IInstanceControlApi 生命周期: 建/删/启/停/重启/切模型 |
| `kCapabilityInstanceTuning` | `instance_tuning` | （S2）IInstanceControlApi 调参: 缩放/布局/动作 |
| `kCapabilitySettingsWrite` | `settings_write` | （S2）ISettingsApi 白名单键写入 |

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

## 模板

`plugins/sdk-template/` 是活着的最小插件（示例页面 + manifest + CMake），拷贝
即得正确工具链与布局约定。第三方面向 `IUiApi.registerPage` 的 qmlUrl 一律使用
`qrc:/` URL（§B.6）。

## 版本历史

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
