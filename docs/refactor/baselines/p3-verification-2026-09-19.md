# P3 验证报告（2026-09-19，Linux 侧）

> 对照设计文档 §D P3 验收门: 每步测试绿 + 截图 QA + 退出 QA + COLD_START_MS 不劣化 >5%。
> 提交: 37a18e9（M1 ScreenshotRunner）、150be9f（M2 PanelApplication）、8292cfd（M3 PanelUiBoot）、0c98bc2（M4 归位+main≤100）、137a3de（--self-quit 退出 QA）。

## 1. main.cpp 拆解全程

| 步 | main.cpp 行数 | 产出 |
|---|---|---|
| P2 后基线 | 586 | — |
| M1 | 445 | src/app/ScreenshotRunner.{hpp,cpp}（token 流验证纯搬家） |
| M2 | 407 | src/app/PanelApplication.{hpp,cpp}（五服务 parent 树; 析构注释按 v2 范式改写） |
| M3 | 211 | src/app/PanelUiBoot.{hpp,cpp}（**17/17 context property 名与顺序机械 diff 相等**） |
| M4 | **86**（终态 106 含 --self-quit） | src/app/AppFontGuard.{hpp,cpp} + salvo/PM/autoStart 归位 |

组合根结构: main = 纯编排（样式 env → 冷启动锚点 → QApplication → Logging → FontGuard → PanelApplication → engine → PanelUiBoot.registerAll → loadFromModule → autoStart → ScreenshotRunner → exec）。

## 2. 验收结果（每步全绿，最终态汇总）

| 门 | 结果 |
|---|---|
| 41/41 测试 | M1–M4 每步提交后独立跑绿 |
| 截图等价 | 全程尺寸带漂移 ≤±16B（52541/40151/93376 @M4）; 零 QML 错误; 17 属性零 ReferenceError |
| **退出 QA（M4 首启）** | `--self-quit 3000`（offscreen）→ **EXIT=0**; 正常关闭路径完整执行（"WsServer: stopped listening" 析构链日志）; **零 QThreadStorage 警告**（P0 基线时存在——M 系列生命周期整理的意外收益） |
| COLD_START_MS | M3=470 → M4=252（同模式同机; 无劣化，方向有利） |
| LOC 门 | 提交 1 时点 86 ≤ 100 ✓ |

## 3. 生命周期语义记录（工程教训入库）

- 跨子树析构顺序 = main 栈控制（PanelApplication 先声明后析构 → 服务树最后死，与旧栈布局等价）
- PanelUiBoot 挂 engine 下：桥接在 QML 上下文销毁后、引擎析构中死亡——存活期仍长于其服务的上下文; 挂 qApp 会颠倒桥接/服务顺序（刻意避免）
- InstanceSession 内部 parent=nullptr + qDeleteAll 语义**未动**（该层级双删风险仍在，注释保留）
- Qt 子对象逆序析构 = 6.6+ 实现行为非契约（§B.2 v2 假设，各挂载点已注释）

## 4. 退出 QA 成为常设门禁

`QT_QPA_PLATFORM=offscreen <exe> --self-quit <ms>` → 断言退出码 0。后续所有 M 级/结构级变更的必跑项（Windows 侧 CI 覆盖）。

## 5. 遗留

- P4（插件框架）→ P5（DownloadService）按序推进（tests/CMakeLists.txt 与 src/api/ 共享写面，串行更稳）; QML 插件管理节视觉打磨可后置 @designer
- Windows 侧 + CI 首跑待 push
