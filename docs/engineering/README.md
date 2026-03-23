# 工程化

> 本文档描述开发语言与工具链、第三方库选型、构建分发方案、项目目录结构、开发环境与代码规范、测试策略。
> 整体架构参见 [架构总览](../README.md)。
>
> **阶段进度**：MVP（渲染引擎）、Phase 1（WebSocket 通信）、Phase 2（Java 控制面板 + 多实例管理）已完成。Phase 3a（语音包挂载 Java 侧）已完成。Phase 3b（音频播放）、Phase 3c（口型同步）、Phase 3d（行为图引擎）待后续实现。

---

## 子文档

| 文档 | 内容 |
|:---|:---|
| [开发语言与工具链](./toolchain.md) | C++ / Java 语言标准、编译器、构建系统、最低系统要求 |
| [第三方库选型](./dependencies.md) | C++ 端 / Java 端依赖库、Cubism SDK 集成详解 |
| [构建与分发](./build.md) | 目标平台、构建命令、分发形式、打包结构 |
| [项目目录结构](./project-structure.md) | 渲染引擎 / 控制面板 / 完整目录结构 |
| [开发环境与代码规范](./coding-standards.md) | IDE 推荐、代码风格、Git 规范 |
| [测试策略](./testing.md) | C++ / Java 单元测试、集成测试、端到端测试 |
