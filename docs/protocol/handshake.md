# 握手与连接建立流程

> 协议概述参见 [通信协议](./README.md)。
> 断连缓存策略参见 [实现参考](./implementation.md)。

---

## 一、正常启动

```plain
控制面板                                     渲染引擎
    │                                           │
    │  PetWebSocketServer.start()               │
    │  监听 ws://localhost:9000                  │
    │                                           │
    │                            WebSocketClient.connect("ws://localhost:9000")
    │                                           │
    │◄──────────── TCP 连接建立 ────────────────│
    │                                           │
    │         event: ready                      │
    │◄──────────── { version, capabilities } ───│
    │                                           │
    │  AppOrchestrator:                         │
    │  stateManager.setConnected(true)          │
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
    │  expectResponse 完成:                     │
    │  restartAttempts = 0                      │
    │  loadModelConfig("Hiyori")                │
    │  startSchedulerForModel("Hiyori")         │
    │                                           │
    │         command: set_position             │
    │──────────── { x: 1200, y: 600 } ────────►│
    │                                           │  glfwSetWindowPos(1200, 600)
    │  flushPendingCommands()                   │
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
    │  stateManager.setConnected(false)          │
    │  scheduler.pause()                        │
    │  后续指令 → sendOrCache():                │
    │    关键指令缓存到 pendingCriticalCommands  │
    │    非关键指令丢弃                          │
    │                                           │
    │                            ... 重连/重启 ...
    │                                           │
    │◄──────────── TCP 连接建立 ────────────────│
    │         event: ready                      │
    │◄──────────── { version, capabilities } ───│
    │                                           │
    │  （同正常启动流程 + flushPendingCommands）  │
```
