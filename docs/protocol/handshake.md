# 握手与连接建立流程

> 协议概述参见 [通信协议](./README.md)。
> 断连缓存策略参见 [实现参考](./implementation.md)。

---

## 关键约束

- 控制面板（Server）**必须先启动并监听端口**，渲染引擎（Client）启动后主动连接
- 控制面板 **必须** 等待 `ready` 事件后才能发送任何 command
- `ready` 事件之前发送的 command，渲染引擎不保证处理

---

## 一、正常启动

```plain
控制面板                                     渲染引擎
    │                                           │
    │  启动 WebSocket Server                     │
    │  监听 ws://localhost:9001                  │
    │                                           │
    │                            连接 ws://localhost:9001
    │                                           │
    │◄──────────── TCP 连接建立 ────────────────│
    │                                           │
    │         event: ready                      │
    │◄──────────── { version, capabilities } ───│
    │                                           │
    │  控制面板:                                 │
    │  标记连接状态 = 已连接                      │
    │                                           │
    │         command: load_model               │
    │──────────── { model_path: "Hiyori" } ────►│
    │                                           │  ChangeScene("Hiyori")
    │         response: load_model              │
    │◄──────────── { success: true } ───────────│
    │                                           │
    │         event: model_loaded               │
    │◄──── { model_id:"Hiyori", motions:[] } ──│
    │                                           │
    │  Response 回执确认:                        │
    │  重置重启计数                              │
    │  加载模型配置("Hiyori")                    │
    │  启动闲时行为调度器                        │
    │                                           │
    │         command: set_position             │
    │──────────── { x: 1200, y: 600 } ────────►│
    │                                           │  设置窗口位置(1200, 600)
    │  重放缓存的关键指令                        │
    │                                           │
    │  ═══════ 正常通信开始 ═══════             │
```

---

## 二、断连恢复

```plain
控制面板                                     渲染引擎
    │                                           │
    │         连接断开（崩溃/网络）              │
    │◄──────── onClose ─────────────────────────│
    │                                           │
    │  标记连接状态 = 已断开                      │
    │  暂停闲时行为调度器                        │
    │  后续指令执行断连缓存策略:                  │
    │    关键指令缓存（见 implementation.md）     │
    │    非关键指令丢弃                          │
    │                                           │
    │                            ... 重连/重启 ...
    │                                           │
    │◄──────────── TCP 连接建立 ────────────────│
    │         event: ready                      │
    │◄──────────── { version, capabilities } ───│
    │                                           │
    │  （同正常启动流程 + 重放缓存指令）          │
```

---

## 三、模型加载失败

```plain
控制面板                                     渲染引擎
    │                                           │
    │  （连接已建立，收到 ready）                │
    │                                           │
    │         command: load_model               │
    │──────────── { model_path: "Bad" } ───────►│
    │                                           │  model_path 校验失败
    │         response: load_model              │
    │◄──────────── { success: false,            │
    │               error_code: 1001 } ─────────│
    │                                           │
    │         event: model_load_failed          │
    │◄──── { error_code:1001, error_message } ──│
    │                                           │
    │  控制面板可选择:                           │
    │  - 使用备选模型重试 load_model             │
    │  - 提示用户模型不可用                      │
    │                                           │
```

---

## 四、ready 超时

若控制面板在 WebSocket 连接建立后 **10 秒内** 未收到 `ready` 事件，建议：
1. 关闭当前连接
2. 判定渲染引擎异常，触发重启流程或向用户报告错误
