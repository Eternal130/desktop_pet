# 开发环境与代码规范

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、开发环境

| 工具 | 推荐 | 说明 |
|:---|:---|:---|
| C++ IDE | CLion / VS Code + clangd 插件 | CLion 原生 CMake 支持；VS Code 配合 clangd 提供代码补全和诊断 |
| QML 开发 | Qt Creator / VS Code + Qt 插件 | QML 语法高亮与预览；注意 Qt LSP 对 C++ 侧存在缺 include 路径的误报，以 `python build.py qt` 实际编译结果为准 |
| 调试器 | GDB / LLDB | |
| 协议测试 | websocat | 命令行 WebSocket 客户端，用于手动测试通信协议 |
| 版本控制 | Git | |

---

## 二、代码规范

**C++**：

| 项目 | 规范 |
|:---|:---|
| 格式化工具 | clang-format（BasedOnStyle: Google，定制缩进为 4 空格） |
| 静态分析 | clang-tidy（启用 `modernize-*`、`bugprone-*`、`performance-*` 检查集） |
| 命名约定 | 类名 `PascalCase`，函数/方法 `camelCase`，常量 `UPPER_SNAKE_CASE`，成员变量 `m_camelCase` |
| 头文件保护 | `#pragma once` |
| include 顺序 | C 标准库 → C++ 标准库 → 第三方库 → 项目内头文件（各组之间空行分隔） |
| 注释 | 公共 API 使用 Doxygen 风格（`///` 或 `/** */`） |

**QML**（controller_qt）：

| 项目 | 规范 |
|:---|:---|
| 文件命名 | QML 文件 `PascalCase`（如 `WelcomePage.qml`），组件注册进 `qt_add_qml_module` 的 `QML_FILES` |
| 布局约束 | `FluFrame` 在 `GridLayout` 中必须显式 `Layout.preferredHeight`/`implicitHeight`；`Row` 子项不可用 `anchors.right`；`ColumnLayout` 无 `topPadding` |

---

## 三、Git 规范

| 项目 | 规范 |
|:---|:---|
| 分支策略 | `main`（稳定发布）+ `dev`（日常开发）+ `feature/*`（功能分支）+ `fix/*`（修复分支） |
| 提交消息 | Conventional Commits 格式：`type(scope): description` |
| type 前缀 | `feat` / `fix` / `refactor` / `docs` / `test` / `build` / `chore` |
| scope 范围 | `renderer` / `controller_qt` / `protocol` / `config` / `ci` 等 |
| 示例 | `feat(controller_qt): add idle behavior scheduler`、`fix(renderer): fix texture leak on model switch` |
