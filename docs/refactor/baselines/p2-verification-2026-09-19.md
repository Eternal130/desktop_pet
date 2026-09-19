# P2 验证报告（2026-09-19，Linux 侧）

> 对照设计文档 §D P2 验收门: 测试全绿；构建时间对比 P0 基线；build/bin 产物等价（poc 移除）；grep 无残留 CMAKE_SOURCE_DIR。
> 提交: 1a63857（P2a SOURCE_DIR 规范化 + P2b poc 退役）、287d1bd + 830168b（P2c 库拆分主体）。

## 1. 交付结构

| 目标 | 类型 | 内容 |
|---|---|---|
| `pet_panel_api` | INTERFACE | 脚手架占位（头文件 P4/P6a 就位；Qt6::Core 接口） |
| `pet_panel_core` | STATIC | main.cpp 以外全部生产源 + **qml module backing target**（已拍板项落地）; include/模块链接/SPDLOG 定义 PUBLIC 上移 |
| `desktop-pet-controller-qt` | exe | 仅 main.cpp，链接 core + **pet_panel_coreplugin**（静态 QML 模块必需） |

## 2. 验收结果

| 门 | 结果 |
|---|---|
| 全新 workflow（41 测试） | 41/41 绿（P2b 后与 P2c 后各跑一轮） |
| CMAKE_SOURCE_DIR 残留 | **0**（187 处替换: 1 + 186） |
| poc | 目标/二进制/源文件全消失; PoCIntegrationTest 按 QSKIP 设计保留 |
| TU 收敛（实测） | 总 .o **302→168（−44%）**; 测试区重复生产 TU **141→0**（设计预期项）; 干净构建 **25.42s→18.87s（−26%）** |
| 41 测试改链接 | 39 整体替换 + 2 按设计从未编译生产码（ProtocolFixtures/PoCIntegration）; **逃生舱 0** |
| build/bin 产物 | 与拆分前等价（无 poc，无增无缺; libpet_panel_core.a 留构建树不部署） |
| QML 运行时 | orchestrator 独立复跑: 3/3 截图、零 unknown type / No module named / failed to load component |

## 3. 设计预警命中记录（重要工程教训）

**§D P2 风险列的"QML 类型注册点归属"真实复现**: backing target 迁至静态库后，exe 不再隐式获得模块插件——运行期 `No module named "DesktopPet"`、**编译零症状**、exe 因空 rootObjects 挂起（超时捕获）。修复 = exe 显式链接 `pet_panel_coreplugin`（其 _init 强制 Q_INIT_RESOURCE 展开）。已记入 AGENTS.md Known Pitfalls。

## 4. 遗留

- Windows 侧 + CI 首跑（672e384）待 push 后验证
- P3（组合根 M1–M4）随后启动; COLD_START_MS 本轮 491ms（screenshot 模式冷启，P3 的 M 系列以正式锚点复测）
