# 测试策略

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、C++ 端（MVP）

| 层次 | 框架 | 覆盖范围 |
|:---|:---|:---|
| 单元测试 | Google Test 1.17.0 | 参数引擎计算、定时器逻辑、坐标转换等纯逻辑模块 |
| 集成测试 | Google Test + 手动验证 | 模型加载全流程 |
| 渲染验证 | 手动截图对比 | 模型渲染正确性（渲染自动化测试成本高，MVP 阶段以手动验证为主） |

---

## 二、Qt 控制面板（controller_qt，Phase 5-9）

| 层次 | 框架 | 覆盖范围 |
|:---|:---|:---|
| 单元测试 | QTest（Qt Test） | **41 个 QTest 二进制**，覆盖全部模块：network（Envelope、WsServer、MessageDispatcher、Handshake、PendingRequests、EventRegistry、ThreadMarshal、ProtocolFactory、WsServerMulti）、core（InstanceSession、InstanceManager、Scheduler、InteractionHandler、RestartController、HitAreaCacheManager、ModelInfoParser、ModelScanner、MountedBehaviorEngine、VoicePackScanner、MetaMkoParser、OggDurationHeuristic、MonitorDataModel、NetworkWiring）、system（AutoLaunchManager、TrayManager、ResourceStatsCollector）、config（InstanceConfig、PanelConfig、InstanceConfigManager、PanelStateManager、ConfigDir、Robustness、PathResolve）、infrastructure（Logging、PoCIntegration、ProcessManager、StartupSalvo、ProtocolFixtures）、气泡流（NotificationStreamModel、NotificationStreamController） |
| 集成测试 | QTest + `REQUIRES_RENDERER` 标签 | 需要真实渲染器二进制的用例带 ctest 标签；渲染器缺失时运行期 `QSKIP`，保证单元层在无头/CI 环境始终可跑 |
| 协议兼容性测试 | QTest | 验证控制器与渲染器两侧 JSON Envelope 消息互解析正确性（含 response 顶层字段验证） |

```bash
ctest --test-dir build/controller_qt            # 全部 41 个 QTest 二进制
ctest --test-dir build/controller_qt -j8        # 并行
ctest --test-dir build/controller_qt -L REQUIRES_RENDERER   # 仅集成层
ctest --test-dir build/controller_qt -E 'REQUIRES_RENDERER' # 仅单元层
```

> **已知偶发失败**（隔离运行均通过）：`SchedulerTest` 并行时序；`WsServerMultiTest::testTwoInstancesRouteCorrectly` 并行负载下偶发 `sendTextMessage returned 0` 竞态。

---

## 三、端到端测试

### 3.1 MVP 测试检查清单

| 场景 | 验证内容 |
|:---|:---|
| 启动流程 | 渲染器启动 → 模型加载 → 透明窗口正常显示 |
| 点击交互 | 点击各 HitArea → 即时动画反馈 |
| 拖拽行为 | 直接跟随模式 → 释放后窗口停留 |
| 闲时动作 | 静置等待 → 定时触发随机闲时动作 → 动画正常播放 |
| 自适应帧率 | 动画播放 60fps → 闲时降至 30fps → 静止降至 15fps |
| 透明窗口 | 模型区域可见、背景完全透明、窗口始终置顶 |

### 3.2 Phase 1-3 增加的测试场景

| 场景 | 验证内容 | 阶段 |
|:---|:---|:---:|
| WebSocket 通信 | 控制面板启动 Server → 渲染引擎连接 → 握手 → 指令收发 | Phase 1 |
| 模型切换 | 切换到新模型 → 加载成功 → 旧模型释放；切换到无效模型 → 失败回退到上一个模型 | Phase 2 |
| 配置持久化 | 修改各项设置 → 完全退出应用 → 重新启动 → 设置保持不变 | Phase 2 |
| 容错恢复 | 手动终止渲染器进程 → 控制面板检测崩溃 → 自动重启 → 恢复模型和窗口位置 | Phase 2 |
| 音频映射 | 配置音频映射 → 下发渲染器 → 动作触发时播放对应音频 | Phase 3 |
| 音频复用 | 多个模型引用同一音频文件 → 均能正常播放 → 音频文件仅存一份 | Phase 3 |
| 音频控制 | 音量调节 → 静音/取消静音 → 动作关联音效同步 | Phase 3 |
