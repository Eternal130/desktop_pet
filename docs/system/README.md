# 系统设计

> 本文档描述容错恢复、配置文件、日志体系、启动流程和扩展性预留。
> 整体架构参见 [架构总览](../README.md)，工具链与版本详见 [工程化](../engineering/README.md)。
>
> **实现状态**：MVP、Phase 1（WebSocket 通信）、Phase 2（控制面板 + 多实例管理）已完成。Phase 3a（语音包挂载 Java 侧）已完成。容错恢复（崩溃重启、断连缓存）已在 `AppOrchestrator` 中实现。配置文件已扩展为多文件分层结构（config.json、panel.json、instances/*.json、mount.json、hit_area_cache.json）。

---

## 子文档

| 文档 | 内容 |
|:---|:---|
| [容错与错误处理](./fault-tolerance.md) | 模型加载失败、渲染器崩溃恢复（指数退避）、WebSocket 断连处理 |
| [配置文件设计](./configuration.md) | config.json、panel.json、instances/*.json、mount.json、hit_area_cache.json、model_config.json、audio_mapping.json |
| [日志体系](./logging.md) | 日志框架、级别定义、文件管理、关键日志点 |
| [启动流程](./startup.md) | MVP 启动、控制面板启动编排、多实例管理、关闭流程 |
| [扩展性预留](./extensibility.md) | Lua 脚本插件系统、养成状态系统 |
| [外置语音包挂载](./voice-pack-mounting.md) | 语音包与模型解耦挂载设计（Phase 3a Java 侧已实现，渲染器侧待实现） |
