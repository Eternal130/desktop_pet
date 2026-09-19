# P1b 验证报告（2026-09-19，Linux 侧）

> 对照设计文档 §D P1b 验收门: 新克隆按 BUILD.md 两条命令全构建成功；构建路径零 Python；41+21 测试全绿。
> 提交: 3493b0c（windeployqt 统一化）、89cedcc（Bootstrap + 旧入口退役）、eec90b2（CJK 字体修复，随附交付）。

## 1. 交付物清单

| 类别 | 内容 |
|---|---|
| 统一化 | `controller_qt/cmake/DeployQtTest.cmake`（pet_deploy_qt_test/pet_deploy_qt_app）；tests/CMakeLists.txt **2053→1113 行**（41 块 → 41 行调用）；主 exe windeployqt 进 CMake |
| Bootstrap | `scripts/Bootstrap.cmake`（Core + GLEW 2.2.0 + GLFW 3.4，幂等/FORCE/PET_REPO_ROOT）；renderer configure 期 Core 守卫 |
| 退役 | 删除 build.py、fetch_cubism_core.sh/.bat；setup-dev-env.ps1 降级可选；build.sh/bat 薄壳转发 |
| 文档 | BUILD.md 全重写（preset 入口/双 MinGW 表/新旧对照）；AGENTS.md STRUCTURE/COMMANDS/pitfalls 更新；全仓 build.py 注释引用清扫（13 文件，零行为变更） |
| 附带 | ScreenshotRunner 落盘 bug 修复（mkpath + 保存校验 + 失败退出码）；CJK 字体修复（见 §4） |

## 2. 验收结果（全部通过）

| 门 | 结果 |
|---|---|
| `cmake --workflow --preset linux-release`（controller） | exit 0，**41/41 测试** |
| `cmake --workflow --preset linux-gl-release`（renderer） | exit 0，**21/21 测试** |
| Bootstrap 幂等（真实环境） | 三项全跳过 + STATUS 说明，exit 0 |
| Bootstrap 空目录全流程（PET_REPO_ROOT scratch） | Core 下载→提取→**4 个 marker 字节数与真实仓库逐一相等**（124424/249800/15320/20966）；GLEW/GLFW 布局一致；复跑全跳过 |
| FORCE 语义 | FORCE=1 只作用于 Core（含假 zip 重取路径验证） |
| renderer 守卫双向 | header 缺失 → FATAL 两步提示；存在 → 通过 |
| ScreenshotRunner 矩阵 | 成功（不预建目录）/ mkpath 失败 / 保存失败（只读目录）→ SCREENSHOT_FAILED + exit 1 |
| 构建路径零 Python | 全仓仅剩 run_ui_tests.py（许可的开发侧工具，不在构建路径） |

## 3. 方法论承接

P1a 确立的截图判据沿用: sha256 不可比（同二进制多次运行哈希漂移），用 **尺寸带 + 页面完整 + 目检**。P0 基线截图（tofu 时代）保留为历史记录；**后续参照基线 = screenshots-p1b/**（字体修复后，本目录）。

## 4. CJK 字体修复（用户报告"中文都是方框"）

- 根因: 开发机 `fc-list :lang=zh` 为空 + 应用未自带字体 → 100% 中文 tofu（P0 截图 @observer 确认）
- 修复: 启动时加载 `<appDir>/fonts/NotoSansCJKsc-Regular.otf` 设为应用默认字体（原默认族保留为回退链；失败 WARN 降级不崩）；controller 自有 POST_BUILD 部署字体（单一事实源 renderer/resources/fonts，OFL）
- 验证: @observer 复检通过——三页面全部中文可读（抽查: 主页/实例详情/资源监控/语音包/资源管理、"创建第一个实例"、"退出前确认"等），全角标点正确，中英混排协调
- **已知遗留（独立问题，未在本轮范围）**: emoji 字形仍为 tofu（本机无 emoji 字体，Noto Sans CJK SC 不含 emoji）——侧栏导航图标、搜索图标等；后续可选: 加载 Noto Color Emoji 或改用矢量图标（后者与 B.6 插件图标 qrc 方案天然契合）

## 5. 遗留（Windows 侧 + 后续阶段）

- Windows 实机: windeployqt 函数实跑、build.bat、workflow win-* 全链、Git-MinGW 中毒路径 FATAL 验证（P1d CI 覆盖）
- P1c（工具链合并，已采纳待执行）→ P1d（最小 CI）→ P2（库拆分 + poc 退役 + CMAKE_SOURCE_DIR→PROJECT_SOURCE_DIR 141 处替换）
