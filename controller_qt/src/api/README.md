# Desktop Pet Panel Plugin SDK (`src/api/`)

第一方插件 SDK 接口头集合（P4 落地，P6a 冻结点前允许演进，冻结后变更走版本化）。
设计合同: `docs/refactor/plugin-architecture-and-cmake-migration.md` §A/§B.3。

## 组成

| 头文件 | 内容 |
|---|---|
| `PluginTypes.hpp` | POD/Qt 值类型（InstanceInfo / PackInfo / DownloadRequest / InstallSpec / PageDescriptor）+ PluginError / PluginLogLevel / 宿主 API 版本常量 |
| `IPanelPlugin.hpp` | 插件唯一入口：`initialize(IPluginContext&)` / `shutdown()`；iid `org.desktop-pet.PanelPlugin/1.0` |
| `IPluginContext.hpp` | 宿主服务面：instanceApi / voicePackApi / uiApi / downloadApi / pluginConfigDir / log |
| `IInstanceApi.hpp` | 只读实例花名册 + `IRosterObserver` 纯虚订阅（**禁 std::function**） |
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
- **阶段 2**（二进制插件）: 既有接口 vtable 与 PluginTypes 结构体**冻结**——
  追加虚方法/加字段 = major 破坏；扩展一律走"新接口（新 iid）经
  IPluginContext 新 accessor 暴露"或新类型。

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
capability stub（每个方法返回 `PluginError::Capability`，响亮失败）。**对第三方
进程内插件这是建议性约束而非执行机制**（C++ 可自开 socket）——能力清单的价值
是审计、用户知情与第一方吊销开关。硬隔离需回到进程边界（方案 C，未采纳）。

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
