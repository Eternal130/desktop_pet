# 开发环境与代码规范

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、开发环境

| 工具 | 推荐 | 说明 |
|:---|:---|:---|
| C++ IDE | CLion / VS Code + clangd 插件 | CLion 原生 CMake 支持；VS Code 配合 clangd 提供代码补全和诊断 |
| Java IDE | IntelliJ IDEA | JavaFX Scene Builder 集成、Maven 原生支持 |
| UI 设计 | JavaFX Scene Builder | 可视化拖拽生成 FXML 布局文件 |
| 调试器 | GDB / LLDB（C++）、IntelliJ Debugger（Java） | |
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

**Java**：

| 项目 | 规范 |
|:---|:---|
| 代码风格 | Google Java Style Guide |
| 格式化工具 | google-java-format（通过 `fmt-maven-plugin` 集成到 Maven 构建） |
| 静态分析 | SpotBugs + PMD（Maven 插件，CI 中运行） |
| 命名约定 | 遵循标准 Java 命名惯例（类 `PascalCase`，方法/变量 `camelCase`，常量 `UPPER_SNAKE_CASE`） |
| Null 安全 | 使用 `@Nullable` / `@NonNull` 注解标注方法参数和返回值，配合 IDE 检查 |
| 数据类 | 优先使用 Java 21 Records 定义不可变数据载体（如协议消息、配置模型） |

---

## 三、Git 规范

| 项目 | 规范 |
|:---|:---|
| 分支策略 | `main`（稳定发布）+ `dev`（日常开发）+ `feature/*`（功能分支）+ `fix/*`（修复分支） |
| 提交消息 | Conventional Commits 格式：`type(scope): description` |
| type 前缀 | `feat` / `fix` / `refactor` / `docs` / `test` / `build` / `chore` |
| scope 范围 | `renderer` / `controller` / `protocol` / `config` / `ci` 等 |
| 示例 | `feat(controller): add idle behavior scheduler`、`fix(renderer): fix texture leak on model switch` |
