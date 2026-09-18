# controller_qt UI 测试结果报告(自动化批次 1)

> **文档性质**:自动化测试结果记录,覆盖 `qt-ui-manual-test.md` 中可自动化的 11 项用例。
> 人工-only 用例(拖拽/拉伸/渲染器交互/托盘)不在本批次范围。
>
> **自动化工具**:`controller_qt/tests/ui_automation/run_ui_tests.py`
> (驱动 `--screenshot` 模式 + 日志断言 + 配置注入 + 像素采样)

---

## 一、测试元信息

| 项目 | 内容 |
|:---|:---|
| 测试开始时间 | 2026-08-21 19:20 |
| 测试结束时间 | 2026-08-21 19:24 |
| 测试人 | 自动化(Sisyphus 编排) |

### 1.1 被测版本

| 项目 | 值 |
|:---|:---|
| Git commit (短) | bc2d970(工作区含退出崩溃修复 + 端口探测修复,未提交) |
| Git 分支 | feat/qt-controller-foundation |
| 构建命令 | `python build.py qt` |
| controller-qt.exe 大小 | 7499 KB |

### 1.2 测试环境

Windows 11 / Qt 6.10.0 MinGW / 渲染器 + 10 模型就绪 / 系统托盘可用

---

## 二、结果总览

**11/11 PASS(100%)**。配套 38/38 QTest 全绿。

本轮自动化发现的两个**真实产品 bug**(均已修复):

1. **每次退出必崩(0xC0000005)**:根因是 `Logging::shutdown()` 调用
   `spdlog::shutdown()` 在 main 栈 unwind 前销毁 spdlog 注册表,与后续
   析构(Qt 静态对象/spdlog 静态析构)冲突。修复:仅 flush,不显式
   shutdown;并在 flush 前卸载 Qt 消息桥接 handler。此 bug 影响所有
   退出路径(包括正常点 ✕ 关闭),自项目诞生即存在,因 WER 静默吞掉
   崩溃而未被察觉。
2. **欢迎页端口检测永远红**:自家 WsServer 先监听 9001,环境检测再用
   socket 探测该端口必失败 → "In use — another process is listening"
   从第一天起就是误报。修复:main.cpp 将 `wsServer.listen()` 结果经
   `envChecker.setOwnServerListening()` 注入,自家监听视为就绪。

---

## 三、详细结果

| TC | 标题 | 结果 | 证据 |
|:---|:---|:---:|:---|
| TC-4.1 | 首次启动进入欢迎页 | PASS | canvas=#ffffff sidebar=#f4f4f5(现代简约浅色) |
| TC-4.7 | 窗口几何持久化 | PASS | 日志 `panelX=100 panelY=100 900x600` 恢复 |
| TC-4.9 | 关闭按钮默认退出 | PASS | 退出码 0,进程消失 |
| TC-4.10 | 标题栏导航 | PASS | welcome/monitor/settings 三页截图产出 |
| TC-4.11 | 环境检测全通过态 | PASS | 日志 `...port=true`(修复 #2 后) |
| TC-4.12 | 端口占用异常态 | PASS | 占 9001 后 `not bindable` + `port=false` |
| TC-5.6 | 实例持久化 | PASS | 预置 2 实例 → 日志 `loaded 2 instance(s)` |
| TC-9.2 | 冷启动时间 | PASS | COLD_START_MS=378ms(< 2000 判定线) |
| TC-9.5 | panel.json 损坏恢复 | PASS | 截断 JSON 启动不崩溃,正常退出 0 |
| TC-9.6 | instance JSON 损坏恢复 | PASS | 损坏实例跳过,`loaded 1 instance(s)` |
| TC-9.7 | 端口冲突优雅降级 | PASS | 端口被占时控制器存活,UI 正常截图 |

截图证据目录:`%TEMP%\dpet_ui_tests_*\`(每次运行打印具体路径)。

---

## 四、未覆盖用例(需人工或后续扩展)

- TC-4.2/4.3/4.4(拖拽/八方向拉伸)— 需真实鼠标;可后续用 Win32
  SendInput 合成事件扩展
- TC-4.5/4.6/X.1(主题切换)— **已过时**:主题目录已移除,单一"现代简约"
  主题,建议更新 `qt-ui-manual-test.md` 删除这些用例
- TC-4.8/4.13(最小化/托盘)— 需任务栏/托盘交互
- TC-5.1~5.5(添加实例对话框)— 需对话框 UI 驱动;可后续扩展 QML
  测试钩子
- TC-6.x / 7.8~7.9 / 8.x(渲染器交互、崩溃恢复、布局手势)— 需渲染器
  进程 + 真实鼠标
- TC-9.1(干净机器部署)/ 9.3~9.4(内存/1 小时稳定性)— 环境或时长所限

---

## 五、总体评价

可自动化层全部通过;过程中挖出并修复两个存量 bug(退出崩溃、端口误报),
其中退出崩溃为 P0 级(影响每一次退出)。建议:退出崩溃修复合入后,可
作为发布阻塞项验证的回归基线(`python controller_qt/tests/ui_automation/run_ui_tests.py`)。
