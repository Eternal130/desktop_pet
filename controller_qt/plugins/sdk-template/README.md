# sdk-template — the living plugin template (P4)

拷贝此目录开始编写第一方插件：

1. 目录改名 + 修改 `plugin.json` 的 `id`（反向域名，全仓唯一）；
2. `api_version` 填写目标宿主 API 版本（`pet::kApiMajor.kApiMinor`，见
   `controller_qt/src/api/PluginTypes.hpp`）；
3. 只面对 `pet::` SDK 接口编程（`src/api/`），不 include 宿主内部头
   （§B.3/§B.4 结构性收窄：插件代码量最小化就是防线）；
4. `SamplePage.qml` 演示了页面骨架与 `bridge` 访问模式（§B.5 阶段 1 的
   delegate-context 暴露；阶段 2 换独立 context）。

宿主管线（`controller_qt/CMakeLists.txt`）自动发现 `plugins/*/plugin.json`，
在 configure 期做字段校验（损坏 = FATAL_ERROR），构建期生成工厂表
（`static_plugins.cpp`），并把 §A.2 的 abi 块从当前构建注入（不手写）。

第三方独立构建（阶段 2 开启后）：本目录自带的 `CMakePresets.json` 继承宿主
Qt MinGW 工具链（§A.2 SDK 构建模板）；阶段 1 期间插件随宿主同仓构建，
无需独立 preset。

ABI 纪律与崩溃语义见 `../../src/api/README.md`。
