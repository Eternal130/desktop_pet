# 规则引擎（声明式规则系统）

> 位置: `controller_qt/src/core/RuleEngine.hpp/.cpp`（核心，S8d）、
> `RuleEngineService.hpp/.cpp`（宿主接线，S8e）、插件注册通道 `pet.rules`（S8f，v1.6，
> `src/api/IRuleApi.hpp`）。
> 相关文档: 事件总线契约 `src/api/EventIds.hpp` + `src/api/README.md` §pet.events；
> 本文是规则 JSON schema 的**唯一权威**。

## 1. 概述

规则引擎是一个声明式的「事件 → 条件 → 动作」系统，让面板规则和插件无需写事件处理
代码即可响应宠物的事件流（点击、闲置、动作结束、语音包挂载……）。一条规则是一份
严格校验（strict-in）的 JSON 文档；装载时编译为 `CompiledRule`（求值期零 JSON 键查找），
运行期在事件总线的 Low 层同步求值（lenient-out：未知上下文一律 fail-closed）。

三层结构（S8d 起）:

| 层 | 职责 | 位置 |
|---|---|---|
| 1. schema 解析/校验 | 严格进: 任何违例拒载 + 结构化错误码 | `RuleEngine::compileRule` |
| 2. 装载期编译 | `CompiledRule`（作用域/owner/声明序宿主盖戳） | `RuleEngine::upsertRule` |
| 3. 纯求值内核 | 冷却/one-shot/where/条件（不 mutate） | `RuleEngine::shouldFire` |

宿主接线（S8e `RuleEngineService`）: 差量总线订阅、活动时钟跟踪（idle_for /
since_last_motion 的数据源）、求值上下文组装、动作分发（fail-fast）、装载管线
（面板 kv + 实例 blob）、插件规则的 owner 动作门控与卸载清扫（S8f）。

## 2. 规则 JSON Schema（schema_version = 1）

顶层字段全表（未知顶层字段 = 拒载，strict-in）:

| 字段 | 类型 | 默认 | 语义 |
|---|---|---|---|
| `schema_version` | int | **必填** | 规则文档格式版本；当前唯一合法值 `1`。与 `pet.rules` 族版本（`kRulesApiVersion`）**解耦** |
| `id` | string | **必填** | 规则 id。语法: 点分隔段，每段非空且仅 `[a-z0-9_-]`。**作用域内唯一**（同 id 装载 = 原子替换）。宿主装载后实例规则 id 存储为 `inst.<uuid>.<id>`、插件规则为 `plugin.<pluginId>.<id>`（命名空间化，见 §5） |
| `enabled` | bool | `true` | 禁用的规则不求值、不贡献订阅 |
| `trigger` | object | **必填** | 触发器 `{event, where?}`，见下表 |
| `condition` | array | `[]` | 条件列表（平坦 AND 语义），见 §4.1 |
| `actions` | array | **必填** | 动作序列（1..8 个，按序执行、fail-fast），见 §4.2 |
| `cancel_default` | bool | `false` | 命中后是否置 `event.cancelled = true`（仅可取消事件生效；已知不可取消事件装载时 WARN 不拒载） |
| `priority` | int | `100` | 跨规则排序第一键（升序 = 先求值先发射） |
| `cooldown_ms` | int | `0` | 命中后的冷却窗口（毫秒，墙钟）；窗口内跳过。`0` = 不冷却 |
| `one_shot` | bool | `false` | 每个启用周期只命中一次（禁用→再启用、或整体重启用 `resetOneShotCycles` 时复位） |
| `dry_run` | bool | `false` | 预演模式: 记「would fire」日志、**不发射任何动作、不 cancel_default** |

`trigger` 字段表（`trigger` 内未知字段 = 拒载）:

| 字段 | 类型 | 默认 | 语义 |
|---|---|---|---|
| `event` | string | **必填** | 触发事件 id（事件总线通道 id，词表见 `src/api/EventIds.hpp`）。语法校验（≥3 个非空点段、每段 `[a-z0-9_]`、禁通配符）是**纯语法**的——未知 id 可正常装载（先订后发合法） |
| `where` | object | `{}` | 触发载荷谓词 `{<field>: {op, value?}}`，多字段 AND。见 §4.3 |

## 3. 拒载错误码表（32 项，装载期 strict-in）

`RuleEngine::compileRule`/`upsertRule` 的全部拒载码（稳定标识符，服务装载层原样透出；
求值期未知**从不拒载**——fail-closed 为假 + DEBUG 日志）:

| 错误码 | 触发条件 |
|---|---|
| `schema_version_missing` | `schema_version` 缺失或非数字 |
| `schema_version_unsupported` | 值 ≠ 1（含非整数） |
| `id_missing` | `id` 缺失或非字符串 |
| `id_invalid` | id 不符合点分隔 `[a-z0-9_-]` 形式 |
| `unknown_top_level_field` | 顶层出现封闭集之外的键 |
| `field_type` | `enabled`/`cancel_default`/`one_shot`/`dry_run` 非 bool；`priority`/`cooldown_ms` 非整数；`cooldown_ms` 为负 |
| `trigger_missing` | `trigger` 缺失或非对象 |
| `trigger_unknown_field` | `trigger` 内出现 `{event, where}` 之外的键 |
| `trigger_event_missing` | `trigger.event` 缺失或非字符串 |
| `trigger_event_syntax` | 事件 id 未通过语法校验（≥3 段、每段非空 `[a-z0-9_]`、无通配符） |
| `where_type` | `trigger.where` 非对象 |
| `where_predicate_type` | where 的某字段值非对象 |
| `where_unknown_field` | 谓词内出现 `{op, value}` 之外的键 |
| `where_op_missing` | 谓词缺 `op` 或非字符串 |
| `where_op_unknown` | op 不在 `{eq, ne, in, exists}` |
| `where_value_type` | eq/ne 的 value 非 string/number/bool；in 的 value 非标量数组；任何位置出现 null |
| `where_value_unexpected` | exists 谓词携带了 `value` |
| `condition_type` | `condition` 非数组 |
| `condition_item_type` | 数组元素非对象 |
| `condition_kind_unknown` | kind 不在封闭的 5 种条件枚举内 |
| `condition_unknown_field` | 键在该 kind 的封闭字段集之外 |
| `condition_field_invalid` | op 不在 `{gt, ge}`；`value_s` 缺失/为负；`from_hour`/`to_hour` 缺失/非整数/超出 0-23；`model` 的 value 缺失/为空/非字符串 |
| `actions_missing` | `actions` 缺失或非数组 |
| `actions_empty` | 空数组（或编译产物无动作） |
| `actions_too_many` | 超过 `kRuleMaxActionsPerRule`（8） |
| `action_item_type` | 数组元素非对象 |
| `action_kind_unknown` | kind 不在封闭的 5 种动作枚举内 |
| `action_unknown_field` | 键在该 kind 的封闭字段集之外 |
| `action_field_invalid` | 必填字段缺失/类型错/为空（group、pack、id、text、event）；`index`/`duration_ms` 非整数或为负 |
| `publish_event_prefix` | `event` 缺少 `pet.rules.` 前缀 |
| `publish_event_syntax` | `event` 未通过事件 id 语法校验 |
| `max_rules_exceeded` | 在 200 条上限（`kRuleMaxRules`）时 upsert 一个**新** id（容量内替换既有 id 合法） |

## 4. 条件 / 动作 / 谓词词汇表

### 4.1 条件（`condition[]`，AND 语义，全部 fail-closed）

| kind | 字段 | 语义 | 未知上下文行为 |
|---|---|---|---|
| `idle_for` | `op`(gt/ge), `value_s`(秒, ≥0) | 距最近一次用户活动（hit/drag_ended/非 idle 组的 motion_finished）的时长超阈值 | lastActivityMs 未知 → false |
| `since_last_motion` | `op`(gt/ge), `value_s`(秒, ≥0) | 距最近一次 motion_finished 的时长超阈值 | lastMotionMs 未知 → false |
| `time_of_day` | `from_hour`, `to_hour`(0-23, 双端含) | 当前小时落在窗口内；`from > to` 是合法跨夜形（22→6 覆盖 22:00–06:59），`from == to` 退化为单小时 | currentHour 未知 → false |
| `model_is` | `value`(非空 string) | 实例当前模型名**精确**（区分大小写）匹配 | modelName 未知 → false |
| `pack_mounted` | `value`(string, `""`=要求未挂载) | 实例当前挂载的语音包目录名匹配 | `""` 挂载态是合法状态（非未知），不 fail-closed |

### 4.2 动作（`actions[]`，按序执行，fail-fast: 一个失败 WARN 并中止该规则剩余动作；命中仍记账）

| kind | 字段 | 语义 |
|---|---|---|
| `play_motion` | `group`(必填非空), `index`(默认 0) | 经共享 `pet::ITuningApi` 向触发实例播放动作组（渲染器忽略未知 group/index）。**插件规则受 owner 门控**（§6.3） |
| `set_expression` | `id`(必填非空) | 经共享 `pet::ITuningApi` 应用表情（渲染器忽略未知 id）。**插件规则受 owner 门控**（§6.3） |
| `play_pack_audio` | `pack`+`group`(必填非空) | 语音包音频: **挂载校验**（快照的 mountedPackId 必须就是该包）→ 双源扫描反解目录 → 解析 meta.mko → `buildAudioOnlyCommand` 语义组装 play_audio 信封经渲染器通道发送。按设计**不**受 owner 门控（挂载校验即其权威边界） |
| `notify_bubble` | `text`(必填非空), `duration_ms`(默认 0) | 经注入的气泡汇（NotificationStreamController）弹出通知。不门控 |
| `publish_event` | `event`(必填, `pet.rules.` 前缀 + id 语法), `payload`(默认 `{}`) | 以**宿主身份**把事件发回总线（前缀编译期已校验——插件不能借规则伪造任意通道）。不门控 |

### 4.3 where 谓词（触发载荷过滤，类型敏感）

| op | 语义 |
|---|---|
| `eq` | 载荷字段 == value（**类型敏感**: JSON 字符串永不等价数字/bool，`"5" != 5`） |
| `ne` | `!eq`（类型不匹配满足 ne） |
| `in` | 载荷字段 == 标量数组任一元素（同类型敏感规则） |
| `exists` | 载荷键存在且非 null（显式 JSON null 视为缺失——与 stats_state 的 gpu/vram null 哨兵同一约定） |

## 5. 装载来源与作用域

| 来源 | scope | 存储位置 | 装载/清除时机 | 存储 id 命名空间 |
|---|---|---|---|---|
| 面板规则 | `Panel` | `app.db` panel_config kv 行 `rules`（`{"rules":[...]}`） | 面板启动时装载（编辑需重启面板生效） | 原样 `<id>` |
| 实例规则 | `Instance` | `app.db` instance_configs 行内 JSON blob 的 `rules` 数组（**裸读**——InstanceConfig serde 会丢弃未知字段） | 启动装载 + roster 差量（新实例装载/删除实例前缀清扫） | `inst.<uuid>.<id>` |
| 插件规则 | `Plugin` | **不持久化**（内存态） | 插件 initialize() 内经 `pet.rules` 注册；shutdown()/卸载清扫清除 | `plugin.<pluginId>.<id>` |

- 装载是**原子换入**: 全量编译进全新引擎后一次性替换（半拒绝的源不会留下半改状态）。
- 单条拒载 WARN（id + 错误码）并跳过；整个源不可读 ERROR + 空集，**从不崩溃**。
- 跨作用域合并排序: `priority` 升序 → scope 权重升序（Instance(0) < Panel(1) < Plugin(2)）
  → 声明序升序。
- 实例规则只看自己 uuid 的事件；面板/插件规则看全部事件。
- 引擎上限: 单引擎 200 条（`kRuleMaxRules`）、单规则 8 个动作。

## 6. 插件注册通道（`pet.rules`，v1.6 / S8f）

### 6.1 能力声明与注册样例

规则注册**不要求任何能力词**（注册是惰性动作）；动作发射才门控（§6.3）。但要让
play_motion/set_expression 动作真正生效，manifest 必须授 `instance_tuning`:

```json
// plugin.json
{
  "id": "org.desktop-pet.greeter",
  "version": "1.0",
  "api_version": "1.6",
  "order": 100,
  "capabilities": ["instance_tuning"]
}
```

```cpp
// 插件 initialize() 内（sdk-template 模式）
pet::PluginError initialize(pet::IPluginContext& ctx) override
{
    auto* api = dynamic_cast<pet::IRuleApi*>(
        ctx.queryApi(pet::kRulesApiId, 1));   // 未接线 → nullptr，必须判空
    if (api == nullptr)
        return pet::PluginError::Ok;          // 宿主无规则引擎 = 特性缺席，非错误

    QJsonObject rule;
    rule.insert("schema_version", 1);
    rule.insert("id", QStringLiteral("pack-greet"));   // 裸 id，宿主自动盖前缀
    QJsonObject trigger;
    trigger.insert("event", QString(pet::kEventPackMounted));
    trigger.insert("where", QJsonObject{
        {"pack_id", QJsonObject{{"op", "eq"}, {"value", "ns_pack"}}}});
    rule.insert("trigger", trigger);
    rule.insert("actions", QJsonArray{
        QJsonObject{{"kind", "set_expression"}, {"id", "smile"}}});
    return api->registerRule(rule);           // Ok / InvalidArgument（原因经 context log）
}

void shutdown() override
{
    // 契约: shutdown 前注销全部规则（宿主有卸载清扫安全网，但残留会 WARN）
    if (auto* api = dynamic_cast<pet::IRuleApi*>(
            m_ctx->queryApi(pet::kRulesApiId, 1)))
        api->unregisterRule(QStringLiteral("pack-greet"));
}
```

### 6.2 身份规则（per-plugin `RuleApiProxy` 强制，非荣誉系统）

- `registerRule`: 存储 id 强制 `plugin.<ownId>.<id>` 前缀 + `RuleScope::Plugin` +
  ownerUuid=插件 id。同裸 id 重复注册 = **原子替换自己的**注册；与其它 owner 的存储 id
  冲突 → InvalidArgument（嵌套插件 id 以存储规则 ownerUuid 复核，不信任 id 前缀本身）。
- `unregisterRule`: 传**裸 id**，代理重新盖前缀并按 ownerUuid 复核后删除——只能删到
  自己的规则；未知 id 合法 no-op。
- schema 校验失败 → InvalidArgument（结构化原因经插件 context log），引擎状态不变；
  超 200 条上限时新 id 同样 InvalidArgument（容量内替换仍合法）。

### 6.3 owner 动作门控矩阵（发射时活查，S8f 设计裁决）

规则 owner × 动作 × 能力 × 写开关 → 结果（对**插件规则**在每次动作发射时现查；
宿主规则永不受门控）:

| owner | 动作 kind | owner 有 `instance_tuning` | 写开关 | 结果 |
|---|---|---|---|---|
| 插件 | play_motion / set_expression | ✔ | 开 | **放行**（经 ITuningApi 发射） |
| 插件 | play_motion / set_expression | ✘（或 owner 未知/查询器未接线） | 任意 | **拒**: 该动作失败 WARN（fail-fast 中止剩余动作；命中仍记账） |
| 插件 | play_motion / set_expression | ✔ | 关 | **拒**（同上） |
| 插件 | notify_bubble / play_pack_audio / publish_event | 任意 | 任意 | **不门控**（气泡是通知面；pack 音频由挂载校验约束；publish_event 是宿主身份 + 编译期前缀校验） |
| 面板 / 实例（宿主） | 全部 | — | — | **永不门控** |

实现: `RuleEngineService` 注入能力查询器（PanelApplication 以 PluginRegistry 前缀匹配
实现——插件 id 带点，服务侧无法无歧义切分；未知 owner fail-closed）与写开关活读
provider（`plugin_write_enabled` kv，缺省=开）。**比 queryApi 时点门控更强**: 授权被撤销
（能力词移除或开关关闭）后，已注册的滞留规则从下一次发射起即被拒，不存在
v1.2/v1.3 写族「缓存指针绕过」的窗口。

### 6.4 生命周期

- 插件规则**不持久化**: 随注册存活，面板重启后由插件 initialize() 重新注册。
- 插件应在 shutdown() 前注销全部规则；`PluginHost::shutdownAll` 在 shutdown() 之后、
  delete 实例之前按 `plugin.<id>.` 前缀清扫残留（非零 WARN——清扫是安全网不是契约，
  与事件订阅卸载清扫同位同义）。

## 7. 触发优先级阶梯（同一事件的总线分发顺序）

```
优先级值小 = 先分发
  Highest(0) ─┐
  High(1)     ├─ 插件监听器（pet.events 订阅，Normal+ 层可 claim 可取消事件）
  Normal(2)  ─┘
  Low(3)     ── 规则引擎（RuleEngineService，全部规则；cancel_default 在此生效）
  Lowest(4)  ── S8b 默认行为决策适配器（MountedBehaviorAdapter 先、InteractionFallbackAdapter 后；
                自查 cancelled —— 被 claim/取消则跳过默认反应）
```

含义: 插件先于规则看到事件（可先行 claim），规则先于默认行为（规则可
`cancel_default` 抑制 MBE/IH 的默认反应），默认行为永远最后。`publish_event` 动作
以宿主身份发回总线（受总线重入深度上限 8 约束，超限 Busy → fail-fast）。

## 8. 北极星示例规则

```json
// ① 久别问候: 摸头且闲置超 5 分钟 → 播放 flick + 气泡 + 抑制默认反应
{
  "schema_version": 1,
  "id": "north-star-1",
  "trigger": {
    "event": "pet.instance.hit",
    "where": { "area_id": { "op": "eq", "value": "head" } }
  },
  "condition": [ { "kind": "idle_for", "op": "gt", "value_s": 300 } ],
  "actions": [
    { "kind": "play_motion", "group": "flick" },
    { "kind": "notify_bubble", "text": "好久不见", "duration_ms": 4000 }
  ],
  "cancel_default": true
}
```

```json
// ② 语音包应答: tap 动作结束 → 播放挂载包 ns_pack 的 tap 组音频
{
  "schema_version": 1,
  "id": "north-star-2",
  "trigger": {
    "event": "pet.instance.motion_finished",
    "where": { "group": { "op": "eq", "value": "tap" } }
  },
  "actions": [ { "kind": "play_pack_audio", "pack": "ns_pack", "group": "tap" } ]
}
```

```json
// ③ 插件挂载致意（§6.1 的完整规则）: 挂载 ns_pack → 微笑表情（owner 门控活查）
{
  "schema_version": 1,
  "id": "pack-greet",
  "trigger": {
    "event": "pet.pack.mounted",
    "where": { "pack_id": { "op": "eq", "value": "ns_pack" } }
  },
  "actions": [ { "kind": "set_expression", "id": "smile" } ]
}
```

## 9. 已知边界

- **one_shot 不持久化**: 启用周期是内存态（启用/整体重启用时复位），面板重启后
  one-shot 规则可再次命中。
- **实例删除随删**: 实例从 roster 消失时其实例规则被前缀清扫（`inst.<uuid>.`），
  不可恢复。
- **编辑需重启面板**: 面板 kv 规则只在启动时装载（运行时规则面 = upsertRuleJson /
  pet.rules 注册通道）；修改 `rules` kv 行不会热生效。
- **面板规则的运行时通道**: 共享 `RuleApiImpl` 的宿主面（registerRule = Panel 作用域）
  是对称性/测试表面，宿主的主装载通道仍是文件管线。
- **规则总开关**: 全局规则 on/off 开关（`resetOneShotCycles` 挂点）尚无 UI 入口，
  预留未接（TODO(rules-master-toggle)）。
- **play_pack_audio 的挂载校验是动作时的快照**: 卸载后到达的旧事件求值时按当前
  mountedPackId 判定（fail-fast WARN），不会播出用户已卸载的包。
