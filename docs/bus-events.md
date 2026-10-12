# 事件总线（pet.events）事件目录

> 位置: 总线核心 `controller_qt/src/core/EventBus.hpp/.cpp`、载荷 builder
> `core/EventCatalog.hpp/.cpp`、契约头 `src/api/EventIds.hpp`（通道词表）+
> `src/api/IEventApi.hpp`（族接口，SDK v1.5 / S8a）。
> 相关文档: 规则引擎 `docs/rules.md`（总线 Low 层的最大消费者）、
> 插件 SDK 总览 `controller_qt/src/api/README.md` §pet.events、
> WS 事件词表 `docs/protocol/events.md`。
>
> **目录真相 = `src/api/EventIds.hpp`**——本文档是它的派生视图（人读版），
> 两者冲突时一律以头文件为准。载荷键名的代码真相 = `core/EventCatalog.cpp`
> （typed builder；宿主发射点禁止手写键名）。

## 1. 定位：进程内同步通知通道

事件总线是面板进程内的 **Forge 风格同步通知总线**，经 `queryApi("pet.events")`
（`pet::IEventApi`）暴露给插件。它与 WS 事件（`docs/protocol/events.md`）是
**两套刻意分离的词汇表**：

- WS 事件 = 渲染器 → 控制面板的**跨进程**报文（renderer 是发布方）；
- 总线通道 = 控制面板进程内的**进程内**通知（宿主发射点或插件是发布方）。

总线通道 ID 命名的是一条**通道（channel）**，不是某个 WS 事件——但多数宿主
通道以 WS 事件为数据源（WS 事件到达后由 `InstanceSession` 的处理器转发上总线，
载荷经 `EventCatalog` builder 组装）。总线同时是规则引擎（Low 层）与默认行为
决策链（Lowest 层）的挂接点，见 §3。

**单一共享总线**：全进程一条 `EventBus`。`pet.instance.*` 通道的事件会扇出到
**所有**订阅者——包括每个实例自己挂的默认行为适配器（适配器内部按
`event.instanceUuid` 做跨实例过滤）。消费者同样应按 `event.instanceUuid`
过滤自己关心的实例。

## 2. Event 结构与通用规则

`pet::Event`（值类型，`IEventApi.hpp`）:

| 字段 | 类型 | 语义 |
|---|---|---|
| `eventId` | string | 通道 ID（语法见下） |
| `instanceUuid` | string | 实例身份；`""` = 面板级事件 |
| `publisherId` | string | `"host"` 或插件 id（见 §5） |
| `timestampMs` | qint64 | **总线发布时盖戳**（发射者填的值必被覆盖） |
| `cancellable` | bool | 发布方声明：`cancelled` 是否被采纳 |
| `cancelled` | bool | 取消标志（`publish` 返回时为终值） |
| `payload` | QJsonObject | snake_case 键；见各事件字段表 |

**事件 ID 语法**（subscribe 与 publish 双侧校验，违例 `InvalidArgument`）:

- 点分隔通道名，**至少 3 段**（`kEventIdMinSegments = 3`），每段非空且仅
  `[a-z0-9_]`（小写）；
- **无通配符**（`*` 拒绝）——v1.5 没有模式订阅；
- 订阅**未知** eventId 合法（先订后发；宿主之后点亮通道即可）。

**载荷纪律**（冻结于 `EventIds.hpp` 头注释）:

- 键一律 **snake_case**；**字段只加不删不改名**——消费者必须忽略未知键；
- 可选值用**哨兵**不用缺键：数值 `-1`、字符串 `""`（禁 `std::optional` 跨界，
  与 `MonitorSample` 的 -1 哨兵同一纪律）；
- 事件 ID **永不改名、永不移除**（针对 `kEventInstanceHit` 编译的插件必须永远
  收到点击）。

## 3. 触发优先级阶梯与取消语义

同一条通道内按优先级**值升序**分发（Highest 先），同优先级按订阅序（FIFO，
stable）。宿主侧的既定阶梯（与 `docs/rules.md` §7 同一真相）:

```
优先级值小 = 先分发
  Highest(0) ─┐
  High(1)     ├─ 插件监听器（pet.events 订阅；约定占据 Normal+ 层，
  Normal(2)  ─┘  在默认行为之前看到并 claim 可取消事件）
  Low(3)     ── 规则引擎（RuleEngineService；全部规则在此求值，
                 cancel_default 在此生效）
  Lowest(4)  ── S8b 默认行为决策适配器（MountedBehaviorAdapter 先注册先收到、
                 InteractionFallbackAdapter 后；两者自查 cancelled ——
                 被 claim/取消则跳过默认反应）
```

- 插件**语法上**可任选优先级（`EventPriority` 五档全开放），但约定插件占
  Normal+；Low 留给规则引擎、Lowest 留给默认行为适配器（两者均在宿主装配时
  订阅：`RuleEngineService` 以 `Low`、两个适配器以 `Lowest` 且 MBE 先、IH 后）。
- 阶梯含义: 插件先于规则看到事件（可先行 claim），规则先于默认行为（规则可
  `cancel_default` 抑制 MBE/IH 的默认反应），默认行为永远最后。

**取消语义**:

- 只有发布时声明 `cancellable = true` 的通道采纳取消——当前词表中**仅
  `pet.instance.hit` 与 `pet.instance.idle_tick`** 两个（指定的 pre-action
  门，S8b 起被消费；其余 10 条通道恒为 `false`，见 §6 各节）。
- 任何优先级的监听器在可取消事件上写 `event.cancelled = true` 都被采纳——
  总线核心不按优先级限制取消权。标志**贯穿整链**（同一 `Event&` 引用下传）:
  后序监听器能看到先序监听器的决定，也能改写它；`publish` 返回时的**终值**
  才是结果。
- **取消不缩短链**: 后序监听器照常收到事件（v1.5 无 mid-chain 短路）。
- **不可取消事件上写 `cancelled` 无效**: 总线在每个监听器调用后复位该标志
  （不报错），`publish` 返回时恒为 `false`。
- 取消的两类消费点:
  - `pet.instance.hit` —— **poster 不消费**返回值；两个 Lowest 适配器各自
    自查 `cancelled`，被取消则跳过语音包行为 / 默认 hit→motion 反应；
  - `pet.instance.idle_tick` —— **poster 消费**返回值（调度回调读取终值，
    被取消则本次 idle `play_motion` 不发出）。

## 4. 同步分发语义

- `publish()` **同步**: 返回时该通道的整条监听器链已跑完、`cancelled` 已终值。
  无队列、无线程跳变、无异步投递——监听器运行在发布者的调用栈中（GUI 线程），
  必须**快进快出、禁阻塞**（阻塞监听器 = 阻塞整个面板事件循环）。
- **快照迭代**: publish 迭代订阅表快照——监听器内可安全订阅 / 退订（自己或
  他人）/ 再发布；已在快照中的监听器恰好收到一次调用。
- **异常隔离**: 每个 `onEvent` 调用点 `try/catch(...)` 兜底——抛异常的监听器
  被 ERROR 记日志，扇出继续（与 PluginContextImpl 的 observer 纪律同源）。
- **重入深度上限 8**（`kEventMaxPublishDepth`，per-thread）: 嵌套 publish 链
  达到上限的那次 publish 返回 `PluginError::Busy` + WARN、零分发——失控的
  监听器反馈环无法挂死 GUI 线程（规则引擎的 `publish_event` 动作同样受此
  约束，超限即 fail-fast）。
- 发布时序细节: eventId 语法违例 → `InvalidArgument`、零分发（不盖时间戳）；
  合法但**无订阅者** → 正常盖戳并返回 `Ok`（"已投递"）。

## 5. 防伪造与命名空间

| 发布者 | 允许的通道 | 机制 |
|---|---|---|
| 插件 | 仅 `plugin.<ownId>.*` | per-context `EventApiProxy` 前缀预检：越界 → `InvalidArgument`、零分发；并**强制盖戳** `publisherId = <ownId>` |
| 宿主 | 任意语法合法的 ID | `pet.*` 是宿主词汇表（`EventIds.hpp` 冻结）；宿主发射点自盖章 `publisherId = "host"`（`kEventPublisherHost`） |

- 防伪造边界: 插件**不能**冒充宿主通道（如 `pet.instance.hit`）——代理在
  转发前拒绝；共享 `EventApiImpl` 从不覆写 `publisherId`（该字段归发射者
  所有，只有 per-plugin 代理强制盖自己的 id）。
- `publisherId` 同时是卸载清扫的依据: 插件应在 `shutdown()` 前退订全部监听器；
  `PluginHost::shutdownAll` 之后按 `unsubscribeByPublisher(pluginId)` 清扫
  残留订阅（非零 WARN——清扫是安全网不是契约）。监听器生命周期**永远归插件**，
  宿主只调不删。

## 6. 事件目录（12 条宿主通道，v1.5 冻结词表）

三域划分: `pet.instance.*`（8）/ `pet.model.*`（2）/ `pet.pack.*`（2）。
全部为**实例级**通道——实例身份由 Event 头的 `instanceUuid` 携带，载荷从不
重复它；面板级事件（`instanceUuid = ""`）在 v1.5/v1.6 词表中暂无宿主通道。
载荷镜像 WS 事件的通道，字段表与 `docs/protocol/events.md` 逐字对齐；宿主
增补字段单独标注。

### 6.1 `pet.instance.ready`（kEventInstanceReady）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `ready`（渲染器握手完成，`InstanceSession`） |
| 频率 | 每次连接一次（含断线重连后的重新握手） |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `version` | string | 渲染器协议版本（"1.0.0"） | — |
| `capabilities` | string[] | 渲染器能力列表（["live2d"]） | — |

载荷为 WS `ready` 载荷的**逐字拷贝**（`makeReadyPayload` 直通——总线不加、
不减）。

### 6.2 `pet.instance.hit`（kEventInstanceHit）

| 项 | 值 |
|---|---|
| 可取消 | **是**（指定的 pre-action 门；S8b 起两个 Lowest 适配器自查 `cancelled`） |
| 来源 | WS `hit`（`handleHitEvent` 纯发射——决策链已总线化） |
| 频率 | 用户点击驱动（频率取决于交互） |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `area_id` | string | 命中区域标识（"head"/"body"，小写） | WS 缺失该半侧时退化为 `""` |
| `x` | double | 鼠标 X（窗口客户坐标） | 缺失退化为 0.0 |
| `y` | double | 鼠标 Y（窗口客户坐标） | 缺失退化为 0.0 |
| `button` | int | 鼠标按键（0 = 左键） | 缺失退化为 0 |

键与 `events.md` §hit 逐字一致。WS 载荷缺失的半侧由发射点退化为零值
（畸形 hit 为 no-op，与 S8b 前行为一致）。

### 6.3 `pet.instance.motion_started`（kEventInstanceMotionStarted）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `motion_started` |
| 频率 | 每次动作开始一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `group` | string | 动作组名 | — |
| `index` | int | 组内序号 | — |

### 6.4 `pet.instance.motion_finished`（kEventInstanceMotionFinished）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `motion_finished`（**双 wire 变体归一为同一形状**） |
| 频率 | 每次动作结束一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `group` | string | 动作组名 | `""` = play_motion_ext 变体 |
| `index` | int | 组内序号 | `-1` = play_motion_ext 变体 |
| `motion_path` | string | 外部动作文件路径 | `""` = play_motion 变体 |

两个 builder 重载**恒发同样的三个键**（哪一半带哨兵即变体判别式），消费者
看到的是单一归一形状。

### 6.5 `pet.instance.idle_tick`（kEventInstanceIdleTick）

| 项 | 值 |
|---|---|
| 可取消 | **是**（poster 消费终值：被取消则跳过本次 idle `play_motion`） |
| 来源 | **宿主点**——闲时调度器 `Scheduler` 的触发回调（非 WS 事件）；在 idle `play_motion` 发出**之前**发布 |
| 频率 | 每个调度节拍一发（节拍间隔 = 实例配置 `idle_interval` 秒，默认 10，范围 1..60） |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `group` | string | 配置的闲时动作组（默认 "Idle"） | — |
| `index` | int | 0..count-1 随机序号 | — |

### 6.6 `pet.instance.audio_finished`（kEventInstanceAudioFinished）

| 项 | 值 |
|---|---|
| 可取消 | 否（面板侧纯遥测——S8b 只接线发射，无本地行为） |
| 来源 | WS `audio_finished`（`events.md` §15：音频**自然播完**才发；`stop_audio` 与被抢占**刻意不发**） |
| 频率 | 每次音频自然结束一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `audio_path` | string | 传给 `play_audio` / `play_motion_ext` 的音频文件路径（原样回显） | — |

载荷为 WS 载荷的逐字拷贝（`makeAudioFinishedPayload` 直通）。

### 6.7 `pet.instance.drag_started`（kEventDragStarted）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `drag_start` |
| 频率 | 每次拖拽手势开始一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `x` | double | 拖拽起点鼠标 X（窗口客户坐标） | — |
| `y` | double | 拖拽起点鼠标 Y | — |

### 6.8 `pet.instance.drag_ended`（kEventDragEnded）

| 项 | 值 |
|---|---|---|
| 可取消 | 否 |
| 来源 | WS `drag_end`；**位置持久化之后**发布（post-commit） |
| 频率 | 每次拖拽松手一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `x` | double | 松手鼠标 X（窗口客户坐标） | — |
| `y` | double | 松手鼠标 Y | — |
| `window_x` | int | 最终窗口左上角 X（屏幕像素） | — |
| `window_y` | int | 最终窗口左上角 Y（屏幕像素） | — |

### 6.9 `pet.model.loaded`（kEventModelLoaded）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `model_loaded` + **宿主增补**（wire 载荷的 motions/expressions 恒空——`events.md` §2，总线载荷改带控制器解析的元数据，即详情页同一份 `ModelInfoParser` 数据） |
| 频率 | 每次 `load_model` 成功一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `model_id` | string | 模型短名（"Hiyori"） | — |
| `motion_groups` | string[] | 解析出的动作组名（宿主增补） | 空数组 = `.model3.json` 解析失败 |
| `expressions` | string[] | 解析出的表情名（宿主增补） | 空数组 = 同上 |

### 6.10 `pet.model.load_failed`（kEventModelLoadFailed）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | WS `model_load_failed`；失败态记录 + `modelLoadFailed` 信号之后发布（post-commit） |
| 频率 | 每次 `load_model` 失败一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `error_code` | int | 渲染器错误码 | — |
| `error_message` | string | 渲染器消息（**逐字**——UI 侧的降级文案是 QML 的事） | `""` = wire 未携带消息 |

### 6.11 `pet.pack.mounted`（kEventPackMounted）

| 项 | 值 |
|---|---|
| 可取消 | 否 |
| 来源 | **宿主点**——`InstanceSession::mountVoicePack` 的 `mountedVoicePackChanged` NOTIFY 处（仅**真实**挂载态变迁触发；重复挂载同一包会重解析但不发） |
| 频率 | 每次挂载成功一发 |

| 字段 | 类型 | 语义 | 哨兵 |
|---|---|---|---|
| `pack_id` | string | 包**目录名**（SDK 全域包身份） | — |

### 6.12 `pet.pack.unmounted`（kEventPackUnmounted）

| 项 | 值 |
|---|---|---|
| 可取消 | 否 |
| 来源 | `InstanceSession::unmountVoicePack` 的同一 NOTIFY 点 |
| 频率 | 每次卸载一发 |

载荷形状与 `pet.pack.mounted` 完全一致（`pack_id` 同上）。

## 7. 「一域一通道」原则

总线只承载**离散通知**（"发生了什么"），不承载数据流（"现在的值是多少"）。
既有数据流各有专属通道、**刻意不进总线**:

| 数据 | 专属通道 | 不进总线的原因 |
|---|---|---|
| 花名册 / 实例状态流 | `IInstanceApi` 的 `IInstanceObserver`（v1.2） | 状态观察是结构化流，有既有的 observer 契约 |
| 监控样本流 | `IMonitorApi` subscribe（v1.4，每订阅者独立节流） | 60 样本环形流 + 节流投递，不是通知语义 |

宿主通道词表因此收敛为三域（`pet.instance.*` / `pet.model.*` / `pet.pack.*`），
且新通道必须先进 `EventIds.hpp`（目录即契约）再接线发射。

## 8. 插件最小样例（IEventListener + queryApi）

```cpp
// 监听器：claim 摸头事件（取消默认反应），快进快出
class HeadPatListener final : public pet::IEventListener
{
public:
    void onEvent(pet::Event& event) override
    {
        // 共享总线会扇出所有实例的事件——先按 instanceUuid 过滤
        if (event.instanceUuid != m_watchedUuid)
            return;
        if (event.payload.value(QStringLiteral("area_id")).toString()
                != QStringLiteral("head"))
            return;
        event.cancelled = true;   // hit 是可取消事件：终值生效，
                                  // Lowest 层的默认反应会被跳过
    }
    QString m_watchedUuid;
};

// 插件 initialize() 内
pet::PluginError initialize(pet::IPluginContext& ctx) override
{
    m_eventApi = dynamic_cast<pet::IEventApi*>(
        ctx.queryApi(pet::kEventsApiId, 1));   // 未接线 → nullptr，必须判空
    if (m_eventApi == nullptr)
        return pet::PluginError::Ok;           // 宿主无总线 = 特性缺席，非错误
    return m_eventApi->subscribe(QString(pet::kEventInstanceHit),
                                 &m_listener, pet::EventPriority::Normal);
}

void shutdown() override
{
    // 契约: shutdown 前退订全部（宿主有卸载清扫安全网，残留会 WARN）
    if (m_eventApi != nullptr)
        m_eventApi->unsubscribe(QString(pet::kEventInstanceHit),
                                &m_listener);
}
```

```cpp
// 发布：插件只能发自己的 plugin.<ownId>.* 通道
pet::Event evt;
evt.eventId = QStringLiteral(
    "plugin.org.desktop-pet.example.state.changed");  // <ownId> 必须是自己的
evt.payload.insert(QStringLiteral("note"), QStringLiteral("hello"));
const pet::PluginError err = m_eventApi->publish(evt);
// err == Ok: 整条监听器链此刻已经跑完（同步分发），evt.cancelled 已终值
// err == InvalidArgument: 通道 ID 语法违例或越出 plugin.<ownId>.* 前缀（零分发）
// err == Busy: 嵌套发布深度已达上限 8（零分发）
```

## 9. 版本与演进规则

- 词表冻结于 **SDK v1.5**（S8a，12 条通道中的 11 条；`pet.instance.audio_finished`
  随 WS `audio_finished` 事件于 S8 旁路阶段补入同一 v1.5 词表）。
- **只加法**: 新通道 / 新载荷字段只能追加；字段永不删改名、事件 ID 永不改名
  移除；可选值一律哨兵（-1 / ""）。
- 消费者必须**忽略未知键**（向前兼容的唯一手段）。
- v1.6（`pet.rules` 规则注册族）未新增总线通道——规则经现有通道触发，其
  `publish_event` 动作以宿主身份发布、且编译期强制 `pet.rules.` 前缀
  （见 `docs/rules.md` §4.2）。
