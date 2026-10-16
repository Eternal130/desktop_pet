# 动态插件（drop-in）加载

> 阶段 2 宿主加载通道（2026-10-16 落地，SDK v1.12）。本文面向"放一个文件装插件"的使用流程；
> 架构决策与开启判据见 `docs/refactor/plugin-architecture-and-cmake-migration.md` §A.2/§A.3/§A.4，
> SDK 接口面见 `controller_qt/src/api/README.md`。

## 目录约定

宿主启动时（静态插件注册之后）按以下优先级扫描，每个目录内按文件名排序处理：

1. `<exeDir>/plugins/` —— 随面板分发的插件（即 `build/bin/plugins/`）
2. `<dataDir>/plugins/` —— 用户投放区（Linux `~/.local/share/desktop-pet/plugins/`，Windows `%LOCALAPPDATA%\desktop-pet\plugins\`）

- 两个目录出现**同名文件**时 exeDir 副本优先，低优先级副本跳过并记日志。
- Linux 只扫描 `*.so`，Windows 只扫描 `*.dll`；目录缺失时静默跳过（宿主**不创建**目录）。
- 插件管理页（侧边栏"资源"组）的"打开插件目录"按钮（QML 侧调用 `pluginManager.openPluginsDir()`）会按需创建并打开 `<dataDir>/plugins/`。

## plugin.json 的动态字段

动态插件的 manifest 嵌在模块元数据里（`Q_PLUGIN_METADATA(IID ... FILE "plugin.json")`，
宿主从 `QPluginLoader::metaData()` 的 `"metadata"` 键读取）。宿主在**实例化之前**依次校验：
iid（须为 `org.desktop-pet.PanelPluginFactory/1.0`）→ manifest 完整 schema → `api_version` →
id 查重（不得与静态插件或其它动态插件撞 id）→ abi 块。

除静态 schema 的全部字段外，动态 manifest 有两个专属键：

| 字段 | 语义 |
|---|---|
| `qml_url` | （v1.12 新增，可选，snake_case）插件页面的 `qrc:/` URL。动态插件没有宿主构建期注入，页面 URL 由 manifest 自带；**静态插件忽略此键**（其 URL 仍由 CMake 注入，注入值优先） |
| `abi` | 五字段块 `{compiler, compiler_version, qt_version, qt_build, build_type}`，与宿主 CMake 生成的基准（`core::hostAbiBlock()`）比对：compiler / compiler_version / qt_build / build_type 严格相等；qt_version 同 major 且插件 minor ≤ 宿主 minor（patch 忽略）；**缺字段 fail-closed 拒载** |

通过 iid 门的文件必有 Failed 条目（错误信息可见，不静默丢弃）；**非 Qt 插件的普通库文件**
（读不到插件元数据）仅日志 WARN 跳过、不进管理列表。关键纪律：**任何校验失败都不会执行库内
代码**——`instance()` 仅在全部校验通过后调用一次（`metaData()` 全程先于 `instance()`）。
已加载的模块带 `PreventUnloadHint`，整个进程生命周期内永不卸载（§A.4 热卸载禁令）。

## 安装 / 更新 / 卸载

1. 把 `.so`（Linux）/ `.dll`（Windows）放进上述任一目录（更新 = 覆盖同名文件；卸载 = 删除文件）；
2. **重启面板**。禁用/启用 = 插件管理页写配置位（`plugin_enabled/<id>`）+ 提示重启生效——
   与静态插件同一语义。如实的禁用边界：禁用条目的 `create()`/`initialize()` **不会执行**，
   但模块的 dlopen 与静态初始化器仍会运行（取舍：同工具链信任域 + 重启生效语义——§A.4
   禁热卸载，禁用检查发生在实例化之前而非加载之前）。

## 同工具链约束

动态插件必须与宿主**同一仓库、同一工具链**编译（Windows: Qt 自带 MinGW 13.1 x64；Linux: 同仓
GCC / Qt 6.10）——abi 门控会明确拒绝任何不匹配的模块（而非静默加载后内存损坏）。插件只编程面向
`pet_panel_api` 纯头接口（禁 `std::` 跨界 / 导出模板 / 接口头 inline 实现，见 `src/api/README.md`
ABI 纪律节），因此无需将 `pet_panel_core` SHARED 化。

模板见 `controller_qt/plugins/sdk-template/`：拷贝目录后以
`-DPET_BUILD_SDK_TEMPLATE_DYNAMIC=ON` 配置即得 MODULE 变体（含 `SamplePluginFactory` 根对象），
**开箱即产出可装载的投放件**——`plugin.json.in` 会在构建期注入本工具链的 abi 块（生成
`plugin_dyn.json` 进 Q_PLUGIN_METADATA），同仓构建天然通过宿主 abi 门。跨仓/换工具链投放仍需
（未来的）第三方 SDK 发布链提供 abi 注入与头文件安装。
