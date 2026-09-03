# 通知流（气泡信息流）

> **状态**：✅ 已实现（取代原渲染器内字幕系统）。
>
> 原字幕系统（libass，渲染器侧渲染，`show_subtitle` 等 5 条指令）已全量移除，
> 文本输出职责迁移至 Qt 控制器侧的桌面级通知流。协议层面旧指令已从渲染器注销
> （未知指令返回 `5003`），参见 [commands.md](../protocol/commands.md) 各「已移除」条目。

---

## 一、概述

通知流是一条**屏幕右上角的桌面级气泡信息流**：无边框、置顶、不抢焦点的小窗口，
多个宠物实例的台词汇入同一条堆叠信息流（新气泡在上，最多 6 条同屏，默认 20 秒自动消失）。

```plain
┌──────────────────────────── 控制器进程（controller_qt） ────────────────────────────┐
│                                                                                     │
│  InstanceSession (每实例)                                                           │
│   └─ MountedBehaviorEngine ──doc 文本──┐                                            │
│                                        ▼                                            │
│                        NotificationStreamController（QML 桥，ctx prop "notificationStream")
│                                        │                                            │
│                                 NotificationStreamModel（QAbstractListModel，cap=6）
│                                        │                                            │
│                            BubbleStreamWindow.qml（独立原生窗口）                   │
│                             └─ BubbleCard.qml × N（气泡卡片）                       │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

**设计要点**：

- **单一共享流**：所有实例的台词进入同一个右上角堆叠流（对齐兽耳桌面端的交互证据），
  不做每实例独立窗口。
- **纯控制器侧**：渲染器零参与——文本不再经 WebSocket 送往渲染器渲染，
  数据路径缩短为 `MountedBehaviorEngine → sink → 气泡`（进程内）。
- **主线程驱动**：全部 QTimer 调度，无工作线程。

## 二、组件

| 组件 | 位置 | 职责 |
|:---|:---|:---|
| `NotificationStreamModel` | `controller_qt/src/ui/NotificationStreamModel.hpp` | 活跃气泡列表模型；`push` 前插、cap 6 淘汰最旧、`expireNow` 过期清理（1s tick）、`dismiss(row)` |
| `NotificationStreamController` | `controller_qt/src/ui/NotificationStreamController.hpp` | QML 桥（ctx 属性 `notificationStream`）；`push`/`dismiss`/`testBubble` |
| `BubbleStreamWindow.qml` | `controller_qt/qml/BubbleStreamWindow.qml` | 独立气泡窗口（`Main.qml` 内声明为子 Window，共享引擎） |
| `BubbleCard.qml` | `controller_qt/qml/components/BubbleCard.qml` | 气泡卡片：头像 + 名字 + 1~2 行文本，点击关闭，入场动画 |

### 窗口标志

```qml
flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
     | Qt.Tool | Qt.WindowDoesNotAcceptFocus
transparent: true
```

- `Qt.Tool`：不进任务栏；`Qt.WindowDoesNotAcceptFocus`：气泡出现不抢焦点（桌宠关键交互）。
- 定位：主屏可用区右上角（右边距/上边距 24px），`visible` 绑定 `count > 0 && enabled`。

## 三、与语音包的联动（唯一台词来源）

语音包行为引擎（`MountedBehaviorEngine`）命中行为时，行为结果携带 `dialogueText`
（来自 meta.mko 的 doc 文本）。`InstanceSession::handleHitEvent` 将其转发至对话 sink：

```plain
MountedBehaviorEngine.buildBehaviorCommand(areaId)
    → BehaviorResult { command, dialogueText }
    → sendCommand(command)            // play_motion_ext（动作+音频，不再带字幕字段）
    → m_dialogueSink(config.id, label(), avatar(), dialogueText)   // 进程内直达气泡
```

这是通知流**唯一的台词来源**（设计修订：对话包系统已整体移除，无预设样式系统；
头像与名字取实例级 `avatar` / `label`）。用户可在实例详情页「对话气泡」卡片点击
「发送测试气泡」（`notificationStream.testBubble()`）验证链路。

sink 经 `InstanceManager::setDialogueSink` seam 注入（模式同 `setAssetRefDetacher`），
`main.cpp` 落接到 `NotificationStreamController`。**注意**：seam 类型使用命名空间级
`using DialogueSinkFn = ...` 别名——moc 无法解析多重 const 引用的 `std::function`
签名（已实测），别名将其视为不透明类型。

## 四、配置

**面板级**（`panel` 配置，见 [configuration.md](./configuration.md)）：

| 键 | 类型 | 默认 | 说明 |
|:---|:---|:---|:---|
| `notifications_enabled` | bool | `true` | 通知流总开关（关闭时窗口隐藏） |
| `notification_duration_ms` | int | `20000` | 气泡默认显示时长（毫秒） |

## 五、测试

QTest 二进制（`ctest --test-dir build/controller_qt`）：

| 测试 | 覆盖 |
|:---|:---|
| `NotificationStreamModelTest` | 前插顺序、cap 6 淘汰、过期清理（确定性 `expireNow(nowMs)` 重载）、dismiss、role 名 |
| `NotificationStreamControllerTest` | push→count 变化、disabled 抑制、testBubble |

## 六、已知限制

- **Wayland**：透明/无边框/置顶窗口在 Wayland（尤其 GNOME）下受限——与项目已知限制
  R4/R5 同类；Windows 为主平台。
- **多显示器**：当前锚定主屏可用区右上角，不做跨屏路由。
- **气泡内容**：v1 为文本 + 头像（emoji 字形或包内图片），不含富媒体/按钮。
