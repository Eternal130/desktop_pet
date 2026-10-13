import QtQuick
import QtQuick.Controls
import QtQuick.Window
import DesktopPet

// Instance detail workbench (design doc §2). Two columns + collapsible log
// drawer. Bindings preserved verbatim; new: third stage tab (hit areas).
Rectangle {
    id: root
    color: "transparent"

    property var instance: instanceManager.instanceAt
                            ? instanceManager.instanceAt(0) : null

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color
    readonly property color _errorColor: Theme.errorColor

    readonly property bool _wide: width >= 900

    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("尚未创建实例 — 请在主页创建")
        color: _mutedColor
        font.pixelSize: 13
    }

    AppButton {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 36
        visible: instance === null
        style: "primary"
        text: qsTr("＋ 创建第一个实例")
        onClicked: createDialog.open()
    }

    // P1-7: model-switch failure notice (AppDialog is plain-text, so the
    // renderer error line is folded into the message).
    AppDialog {
        id: modelFailDialog
        title: qsTr("模型切换失败")
        showNegative: false
        positiveText: qsTr("知道了")
        message: ""
    }

    CreateInstanceDialog {
        id: createDialog
        onAccepted: (name, avatar) => {
            // S5: creation goes through the rosterModel write bridge (the
            // shared IInstanceControlApi — int error code, 0 = Ok); the
            // dialog itself never emits accepted (see its header), this
            // legacy binding stays for safety.
            const err = rosterModel.createInstance(name, avatar)
            if (err === 0) {
                const row = instanceManager.rowCount() - 1
                Window.window.selectInstance(
                    row, instanceManager.instanceAt(row).instanceId)
            }
        }
    }

    // Instance icon picker — same asset grid as the Logo source selector.
    // Selection calls setInstanceIcon; the 🐾 default entry clears (-1).
    Popup {
        id: iconPicker
        anchors.centerIn: parent
        width: Math.min(480, root.width - 64)
        height: Math.min(420, root.height - 64)
        modal: true
        padding: 16

        background: Rectangle {
            radius: Theme.radiusLg
            color: Theme.surfaceColor
            border.width: 1
            border.color: Theme.borderColor
        }

        contentItem: Flickable {
            clip: true
            contentHeight: pickerCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: pickerCol
                width: parent.width
                spacing: 12

                Text {
                    text: qsTr("选择实例图标")
                    color: Theme.textColor
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Flow {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        width: 72; height: 92
                        radius: Theme.radiusMd
                        color: Theme.surfaceColor
                        border.width: 1
                        border.color: Theme.borderColor
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (instance)
                                    assetManager.setInstanceIcon(instance.uuid, -1)
                                iconPicker.close()
                            }
                        }
                        Column {
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: instance ? instance.avatar : "🐱"
                                font.pixelSize: 24
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("默认")
                                color: Theme.text2Color
                                font.pixelSize: 11
                            }
                        }
                    }

                    Repeater {
                        model: assetManager.assets()

                        delegate: Rectangle {
                            id: pickTile
                            required property var modelData
                            width: 72; height: 92
                            radius: Theme.radiusMd
                            color: Theme.surfaceColor
                            border.width: 1
                            border.color: Theme.borderColor
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (instance)
                                        assetManager.setInstanceIcon(
                                            instance.uuid, pickTile.modelData.id)
                                    iconPicker.close()
                                }
                            }
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 44; height: 44
                                    radius: 10
                                    color: Theme.offBgColor
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: pickTile.modelData.fileUrl
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                    }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: pickTile.modelData.name
                                    color: Theme.text2Color
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        visible: instance !== null
        contentWidth: width
        contentHeight: detailCol.implicitHeight + 72
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: detailCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            y: 24
            spacing: Theme.spaceGroup

            // ══════════════ Header bar ════════════════════════════════
            Card {
                width: parent.width
                padding: 16

                Item {
                    width: parent.width
                    height: 36

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        // clickable avatar: image icon when set, emoji otherwise
                        Rectangle {
                            id: avatarButton
                            width: 36; height: 36
                            radius: 18
                            color: Qt.alpha(Theme.accentColor, 0.14)
                            border.width: 1
                            border.color: Theme.borderColor
                            anchors.verticalCenter: parent.verticalCenter

                            readonly property string iconUrl: instance
                                ? assetManager.instanceIconUrl(instance.uuid) : ""

                            Image {
                                anchors.fill: parent
                                anchors.margins: 3
                                source: avatarButton.iconUrl
                                fillMode: Image.PreserveAspectFit
                                visible: avatarButton.iconUrl.length > 0
                                asynchronous: true
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: avatarButton.iconUrl.length === 0
                                text: instance ? instance.avatar : "🐱"
                                font.pixelSize: 16
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: iconPicker.open()
                            }
                        }

                        Text {
                            text: instance ? instance.label : ""
                            color: Theme.textColor
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                        StatusPill {
                            status: instance ? instance.status : "stopped"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            visible: instance && instance.modelName.length > 0
                            width: modelPillText.implicitWidth + 20
                            height: 22
                            radius: 11
                            color: Qt.alpha(Theme.accentColor, 0.1)
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                id: modelPillText
                                anchors.centerIn: parent
                                text: qsTr("模型：") + (instance ? instance.modelName : "")
                                color: Theme.accentColor
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    // Fixed slots (UX fix): lifecycle actions keep stable
                    // positions — start/stop share ONE slot (text + style
                    // swap only, no visible toggling that used to shift
                    // the row), restart stays put and just disables when
                    // not running, and delete is isolated to the far
                    // right behind a 16px gap.
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        AppButton {
                            style: "subtle"
                            text: qsTr("＋ 新建实例")
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: createDialog.open()
                        }
                        AppButton {
                            style: instance && instance.status === "running"
                                   ? "subtle" : "primary"
                            text: instance && instance.status === "running"
                                  ? qsTr("⏹ 停止") : qsTr("▶ 启动")
                            // P2-12: debounce — no lifecycle spam while a
                            // start/stop transition is in flight.
                            enabled: instance && instance.status !== "pending"
                                     && instance.status !== "connecting"
                            anchors.verticalCenter: parent.verticalCenter
                            // S5: writes go through the shared-API bridge
                            // (reads keep the live session object).
                            onClicked: if (instance)
                                instance && instance.status === "running"
                                    ? instanceControl.stop(instance.uuid)
                                    : instanceControl.start(instance.uuid)
                        }
                        AppButton {
                            text: qsTr("↻ 重启")
                            // P2-12: running guard + transition debounce.
                            enabled: instance && instance.status === "running"
                                     && instance.status !== "pending"
                                     && instance.status !== "connecting"
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: if (instance)
                                instanceControl.restart(instance.uuid)
                        }
                        Item { width: 16; height: 1 }
                        AppButton {
                            style: "danger"
                            text: qsTr("🗑 删除")
                            anchors.verticalCenter: parent.verticalCenter
                            // requestDelete expects the uuid STRING; passing
                            // instance.instanceId (an int routing id) never
                            // matched rowForUuid — delete silently no-op'd.
                            onClicked: if (instance)
                                instanceManager.requestDelete(instance.uuid)
                        }
                    }
                }
            }

            // ── Start-failure banner (P1-1) ───────────────────────────
            // Same InfoBar structure as MonitorPage's stale-warning bar,
            // error-flavored: an error instance must be un-missable with a
            // one-click retry. InstanceSession exposes no start-failure
            // reason Q_PROPERTY (only lastModelLoadError, which is
            // model-switch scoped), so the banner states the fact —
            // nothing invented.
            Rectangle {
                width: parent.width
                height: 36
                visible: instance && instance.status === "error"
                radius: Theme.radiusMd
                color: Theme.errorBgColor
                border.color: Qt.alpha(Theme.errorColor, 0.3)
                border.width: 1

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 120
                    elide: Text.ElideRight
                    text: qsTr("⚠ 启动失败 — 渲染进程未能启动或异常退出，可点击「重试」")
                    color: root._errorColor
                    font.pixelSize: 12
                }
                AppButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    style: "subtle"
                    text: qsTr("重试")
                    implicitHeight: 24
                    fontSize: 12
                    // S5: write goes through the shared-API bridge.
                    onClicked: if (instance)
                        instanceControl.start(instance.uuid)
                }
            }

            // ══════════════ Two-column zone (P1-6 Flow) ═══════════════
            // Flow replaces the old Row: wide mode lays the two columns
            // side by side (60/40); narrow mode (<900px) wraps the right
            // column below the left one at full width instead of hiding
            // it. The left width is floor'd so the wide-mode pair sums to
            // exactly the Flow width (no float wrap-flapping).
            Flow {
                width: parent.width
                spacing: Theme.spaceGroup

                // ── LEFT column ───────────────────────────────────────
                Column {
                    id: leftColumn
                    width: root._wide
                        ? Math.floor((parent.width - Theme.spaceGroup) * 0.6)
                        : parent.width
                    spacing: Theme.spaceGroup

                    // Stage card: model + 3 tabs
                    Card {
                        width: parent.width
                        title: qsTr("舞台 · 模型与动作")
                        hint: qsTr("模型切换即时生效 · 渲染后端重启实例后生效")

                        Row {
                            width: parent.width
                            spacing: 8
                            // P1-7: the combo commits on pick — instant
                            // apply is the established semantic, the
                            // redundant "应用" button is gone. The
                            // pre-switch model is recorded here as the
                            // failure bounce-back target (see the
                            // onModelLoadFailed Connections below).
                            AppComboBox {
                                id: modelCombo
                                width: parent.width
                                model: instance ? instance.availableModels() : []
                                onActivated: if (instance && currentText) {
                                    root._prevModel = instance.modelName
                                    instanceControl.loadModel(
                                        instance.uuid, currentText)
                                }
                            }
                        }
                        Text {
                            visible: !(instance && instance.modelLoaded)
                                     && instance && instance.availableModels().length === 0
                            text: qsTr("未找到模型 — 请检查渲染器旁的 Resources/Models/")
                            color: root._faintColor
                            font.pixelSize: 11
                        }

                        // Graphics-backend row: picks which renderer
                        // executable this instance launches ("opengl" /
                        // "vulkan"). The two backends are separate
                        // executables, so a change applies on restart and
                        // never hot-swaps (the ↻ 重启 button is in the
                        // header bar above).
                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                text: qsTr("渲染后端")
                                color: Theme.textColor
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            AppComboBox {
                                id: backendCombo
                                width: 168
                                model: root._backendLabels
                                onActivated: (i) => {
                                    if (!instance) return
                                    const value = root._backendValueAt(i)
                                    if (value.length === 0) return
                                    // Write goes through the shared-API
                                    // bridge like every other write on
                                    // this page. activated() fires on
                                    // user picks only — the programmatic
                                    // currentIndex writes in
                                    // _syncCombos() never re-enter here.
                                    const err =
                                        instanceControl.setGraphicsBackend(
                                            instance.uuid, value)
                                    if (err === 0) {
                                        backendError.text = ""
                                    } else {
                                        backendError.text =
                                            qsTr("设置失败（错误码 %1），后端未更改").arg(err)
                                        // Bounce back to the live value.
                                        backendCombo.currentIndex =
                                            root._backendIndex(
                                                instance.graphicsBackend)
                                    }
                                }
                            }
                            Text {
                                text: qsTr("重启实例后生效")
                                color: root._faintColor
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Text {
                            id: backendError
                            visible: text.length > 0
                            text: ""
                            color: root._errorColor
                            font.pixelSize: 11
                        }

                        Row {
                            width: parent.width
                            spacing: 0
                            visible: instance && instance.modelLoaded

                            StageTab {
                                text: qsTr("动作 %1").arg(
                                    instance ? instance.motionGroupNames().length : 0)
                                tab: 0
                            }
                            StageTab {
                                text: qsTr("表情 %1").arg(
                                    instance ? instance.expressionNames().length : 0)
                                tab: 1
                            }
                            StageTab {
                                text: qsTr("命中区域 %1").arg(
                                    instance ? instance.hitAreaNames().length : 0)
                                tab: 2
                            }
                        }

                        Flow {
                            width: parent.width
                            spacing: 8
                            visible: root.stageTab === 0

                            Repeater {
                                model: (instance && instance.modelLoaded)
                                    ? instance.motionGroupNames() : []
                                delegate: Chip {
                                    text: modelData + " ×" +
                                          instance.motionCount(modelData)
                                    onActivated: instanceControl.playMotion(
                                        instance.uuid, modelData, 0)
                                }
                            }
                        }
                        Text {
                            visible: root.stageTab === 0 && instance
                                     && instance.modelLoaded
                                     && instance.motionGroupNames().length === 0
                            text: qsTr("（该模型未定义动作）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }

                        Flow {
                            width: parent.width
                            spacing: 8
                            visible: root.stageTab === 1

                            Repeater {
                                model: (instance && instance.modelLoaded)
                                    ? instance.expressionNames() : []
                                delegate: Chip {
                                    text: modelData
                                    onActivated: instanceControl.setExpression(
                                        instance.uuid, modelData)
                                }
                            }
                        }
                        Text {
                            visible: root.stageTab === 1 && instance
                                     && instance.modelLoaded
                                     && instance.expressionNames().length === 0
                            text: qsTr("（该模型未定义表情）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }

                        Flow {
                            width: parent.width
                            spacing: 8
                            visible: root.stageTab === 2

                            Repeater {
                                model: (instance && instance.modelLoaded)
                                    ? instance.hitAreaNames() : []
                                delegate: Chip {
                                    text: modelData
                                    onActivated: instanceControl.triggerHitArea(
                                        instance.uuid, modelData)
                                }
                            }
                        }
                        Text {
                            visible: root.stageTab === 2 && instance
                                     && instance.modelLoaded
                                     && instance.hitAreaNames().length === 0
                            text: qsTr("（该模型未定义命中区域）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }
                        Text {
                            visible: root.stageTab === 2
                            text: qsTr("命中区域已缓存，点击可试触")
                            color: root._faintColor
                            font.pixelSize: 11
                        }
                    }

                    // Runtime parameters card
                    Card {
                        width: parent.width
                        title: qsTr("运行参数")

                        ParamSlider {
                            width: parent.width
                            labelText: qsTr("窗口不透明度")
                            descText: qsTr("10% – 100%")
                            // P2-7: percent display — internal 0.1–1.0 value
                            // and the write path are unchanged.
                            valueText: Math.round(
                                (instance ? instance.opacity : 1.0) * 100) + "%"
                            from: 0.1; to: 1.0; stepSize: 0.05
                            bindValue: instance ? instance.opacity : 1.0
                            onMoved: (val) => {
                                if (instance) instanceControl.setOpacity(
                                    instance.uuid, val)
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("音量")
                            descText: qsTr("播放器总音量")
                            Row {
                                spacing: 10
                                AppSlider {
                                    id: volumeSlider
                                    width: 200
                                    from: 0; to: 1; stepSize: 0.05
                                    value: instance ? instance.volume : 1.0
                                    onMoved: (val) => {
                                        if (instance) instanceControl.setVolume(
                                            instance.uuid, val)
                                    }
                                }
                                Text {
                                    // P2-7: percent display — internal 0–1
                                    // value and the write path are unchanged.
                                    text: Math.round(
                                        (instance ? instance.volume : 1.0) * 100) + "%"
                                    color: Theme.accentColor
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                ToggleSwitch {
                                    checked: instance ? instance.muted : false
                                    anchors.verticalCenter: parent.verticalCenter
                                    onToggled: if (instance)
                                        instanceControl.setMuted(
                                            instance.uuid, checked)
                                }
                                Text {
                                    text: qsTr("静音")
                                    color: root._faintColor
                                    font.pixelSize: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("FPS 模式")
                            descText: qsTr("自适应随场景调节")
                            Segmented {
                                options: [qsTr("自适应"), qsTr("固定")]
                                currentValue: (instance && instance.targetFps > 0)
                                    ? qsTr("固定") : qsTr("自适应")
                                onSelected: (v) => {
                                    if (!instance) return
                                    if (v === qsTr("固定") && instance.targetFps === 0)
                                        instanceControl.setFps(instance.uuid, 60)
                                    else if (v === qsTr("自适应"))
                                        instanceControl.setFps(instance.uuid, 0)
                                }
                            }
                        }
                        ParamSlider {
                            width: parent.width
                            labelText: qsTr("目标帧率")
                            descText: qsTr("固定模式下生效")
                            valueText: (instance && instance.targetFps > 0)
                                ? instance.targetFps : qsTr("自适应")
                            from: 15; to: 120; stepSize: 1
                            bindValue: instance && instance.targetFps > 0
                                ? instance.targetFps : 60
                            sliderEnabled: instance && instance.targetFps > 0
                            onMoved: (val) => {
                                if (instance) instanceControl.setFps(
                                    instance.uuid, Math.round(val))
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("待机动作间隔")
                            descText: qsTr("自动播放空闲动作")
                            Text {
                                text: instance
                                    ? (instance.idleIntervalSeconds() + " s")
                                    : "—"
                                color: root._mutedColor
                                font.pixelSize: 13
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("拖拽模式")
                            descText: qsTr("拖动宠物即可移动，位置自动保存")
                            Text {
                                text: instance ? instance.dragMode() : "direct"
                                color: root._mutedColor
                                font.pixelSize: 13
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("自动启动")
                            descText: qsTr("控制面板启动时自动启动该实例")
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: instance ? instance.autoStartEnabled() : false
                                onToggled: if (instance)
                                    instanceControl.setAutoStart(instance.uuid, checked)
                            }
                        }
                    }
                }

                // ── RIGHT column ───────────────────────────────────────
                // P1-6: no longer hidden when narrow — the Flow wraps it
                // below the left column at full width. Card order is the
                // narrow-stack order 布局 → 语音包 → 对话气泡 (wide mode
                // follows the same order down the column).
                Column {
                    width: root._wide
                        ? parent.width - leftColumn.width - Theme.spaceGroup
                        : parent.width
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("布局")

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: root._mutedColor
                            font.pixelSize: 12
                            text: qsTr("宠物窗口内：Shift+拖动 移动 · Shift+滚轮 缩放 · " +
                                       "Ctrl+滚轮 调整窗口。变更自动持久化。")
                        }
                        Row {
                            spacing: 8
                            AppButton {
                                text: qsTr("↺ 重置布局")
                                enabled: instance && instance.modelLoaded
                                onClicked: if (instance)
                                    instanceControl.resetLayout(instance.uuid)
                            }
                        }
                    }

                    Card {
                        width: parent.width
                        title: qsTr("语音包")
                        AppButton {
                            style: "subtle"
                            text: qsTr("管理 →")
                            implicitHeight: 26; fontSize: 12
                            onClicked: Window.window.switchPage("voicepack")
                        }

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: root._mutedColor
                            font.pixelSize: 12
                            text: qsTr("挂载后行为引擎接管命中分发，优先于默认处理。" +
                                       "完整管理已移至「语音包」页面。")
                        }
                    }

                    Card {
                        width: parent.width
                        title: qsTr("对话气泡")

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: root._mutedColor
                            font.pixelSize: 12
                            text: qsTr("台词气泡由语音包行为触发（点击宠物触发动作时显示）。")
                        }

                        Row {
                            spacing: 8
                            AppButton {
                                style: "subtle"
                                text: qsTr("发送测试气泡")
                                implicitHeight: 26; fontSize: 12
                                onClicked: notificationStream.testBubble()
                            }
                        }
                    }
                }
            }

            // ══════════════ Command log drawer ═════════════════════════
            Card {
                id: logCard
                width: parent.width
                // P2-15: collapsed by default (no persistence).
                property bool expanded: false

                Item {
                    width: parent.width
                    height: 36

                    Text {
                        id: logTitle
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("⌨ 命令日志")
                        color: Theme.textColor
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.left: logTitle.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: logCard.expanded
                        text: qsTr("最近 50 条 · 实时")
                        color: root._faintColor
                        font.pixelSize: 11
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        AppButton {
                            style: "subtle"
                            text: qsTr("⧉ 复制")
                            implicitHeight: 26; fontSize: 12
                            onClicked: {
                                let s = ""
                                for (let i = 0; i < logModel.count; ++i)
                                    s += logModel.get(i).time + " → " +
                                         logModel.get(i).action + "\n"
                                textEdit.text = s
                                textEdit.selectAll()
                                textEdit.copy()
                            }
                        }
                        AppButton {
                            style: "subtle"
                            text: qsTr("清空")
                            implicitHeight: 26; fontSize: 12
                            onClicked: logModel.clear()
                        }
                        AppButton {
                            style: "subtle"
                            text: logCard.expanded ? qsTr("收起 ▴") : qsTr("展开 ▾")
                            implicitHeight: 26; fontSize: 12
                            onClicked: logCard.expanded = !logCard.expanded
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: logCard.expanded ? 200 : 0
                    visible: logCard.expanded
                    radius: Theme.radiusSm
                    color: Theme.logBgColor

                    ListView {
                        id: logView
                        anchors.fill: parent
                        anchors.margins: 10
                        clip: true
                        model: logModel
                        spacing: 4
                        ScrollBar.vertical: AppScrollBar {}

                        delegate: Text {
                            width: logView.width
                            text: "<span style='color:#7a7a7a'>" + model.time +
                                  "</span>  <span style='color:#6fbf6f'>→ " +
                                  model.action + "</span>"
                            textFormat: Text.RichText
                            color: Theme.logTextColor
                            font.pixelSize: 11
                            font.family: "Consolas"
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: logModel.count === 0
                            text: qsTr("（尚未发送命令）")
                            color: "#7a7a7a"
                            font.pixelSize: 11
                        }
                    }
                }

                TextEdit {
                    id: textEdit
                    visible: false
                }
            }
        }
    }

    property int stageTab: 0

    // P1-7: model name before the last combo-initiated switch — the
    // failure bounce-back target ("" before any switch from this page).
    property string _prevModel: ""

    ListModel { id: logModel }

    Connections {
        target: root.instance
        ignoreUnknownSignals: true
        function onCommandSent(action) {
            logModel.append({
                time:   Qt.formatTime(new Date(), "hh:mm:ss"),
                action: action
            })
            while (logModel.count > 50)
                logModel.remove(0)
        }
    }

    // P1-7: model-switch failure feedback. C++ keeps the FAILED name in
    // modelName (it only clears modelLoaded — see handleModelLoadFailed
    // Event), so the bounce-back is display-side: re-point the combo at
    // the recorded pre-switch model, same pattern as the model library's
    // per-row rollback.
    Connections {
        target: root.instance
        ignoreUnknownSignals: true
        function onModelLoadFailed(error) {
            if (!instance) return
            if (root._prevModel.length > 0) {
                const i = modelCombo.find(root._prevModel)
                if (i >= 0) modelCombo.currentIndex = i
                modelFailDialog.message = qsTr(
                    "切换模型到「%1」失败，下拉已弹回「%2」。\n%3")
                    .arg(instance.modelName).arg(root._prevModel).arg(error)
            } else {
                // Failure not initiated from this combo (e.g. a pack
                // action) — no bounce target, just the notice.
                modelFailDialog.message = qsTr(
                    "加载模型「%1」失败。\n%2")
                    .arg(instance.modelName).arg(error)
            }
            modelFailDialog.open()
        }
    }

    Connections {
        target: root
        function onInstanceChanged() {
            root._syncCombos()
        }
    }
    Component.onCompleted: root._syncCombos()

    // ── Graphics-backend combo mapping ────────────────────────────────
    // Display labels are fixed proper nouns; committed values are the
    // lowercase contract strings ("opengl" / "vulkan").
    readonly property var _backendLabels: ["OpenGL", "Vulkan"]
    readonly property var _backendValues: ["opengl", "vulkan"]

    // Case-insensitive backend string → combo index (empty/unknown
    // resolves to 0 = OpenGL, the default backend).
    function _backendIndex(backend) {
        const i = root._backendValues.indexOf(
            String(backend || "").toLowerCase())
        return i >= 0 ? i : 0
    }

    // Combo index → lowercase contract value ("" when out of range).
    function _backendValueAt(i) {
        return (i >= 0 && i < root._backendValues.length)
            ? root._backendValues[i] : ""
    }

    function _syncCombos() {
        if (!instance) return
        const i = modelCombo.find(instance.modelName)
        modelCombo.currentIndex = i >= 0 ? i : -1
        // Backend follows the live value (case-insensitive per the
        // contract; C++ owns the lowercase spelling). A programmatic
        // currentIndex assignment never emits activated(), so no
        // write re-enters from this sync.
        backendCombo.currentIndex = root._backendIndex(
            instance.graphicsBackend)
        backendError.text = ""
    }

    // ── Inline components ──────────────────────────────────────────────
    component StageTab : Rectangle {
        id: tab
        property string text: ""
        property int tab: 0
        width: tabLabel.implicitWidth + 28
        height: 32
        // P2-11: hover feedback — subtle wash while hovered, fully
        // transparent otherwise (selection stays the accent underline).
        color: tabMouse.hovered
               ? Qt.alpha(Theme.textColor, 0.06) : "transparent"
        radius: Theme.radiusMd

        Rectangle {
            visible: root.stageTab === tab.tab
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            color: Theme.accentColor
        }
        Text {
            id: tabLabel
            anchors.centerIn: parent
            text: tab.text
            color: root.stageTab === tab.tab
                   ? Theme.accentColor : Theme.text2Color
            font.pixelSize: 13
            font.weight: root.stageTab === tab.tab ? Font.DemiBold : Font.Normal
        }
        MouseArea {
            id: tabMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.stageTab = tab.tab
        }
    }

    component ParamSlider : Column {
        id: pslider
        property string labelText: ""
        property string descText: ""
        property string valueText: ""
        property real from: 0.0
        property real to: 1.0
        property real stepSize: 0.1
        property real bindValue: 0.0
        property bool sliderEnabled: true
        signal moved(real val)
        spacing: 6

        ParamRow {
            width: parent.width
            labelText: pslider.labelText
            descText: pslider.descText
            Row {
                spacing: 8
                AppSlider {
                    width: 200
                    from: pslider.from
                    to: pslider.to
                    stepSize: pslider.stepSize
                    value: pslider.bindValue
                    enabled: pslider.sliderEnabled
                    onMoved: (v) => pslider.moved(v)
                }
                Text {
                    width: 48
                    text: pslider.valueText
                    color: Theme.accentColor
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    component ParamRow : Item {
        id: prow
        property string labelText: ""
        property string descText: ""
        default property alias _content: _slot.data
        // Binding-loop fix (S5): _slot.height used to be childrenRect.
        // height, but vertically-anchored slot children (e.g. the
        // auto-start ToggleSwitch) position their y off _slot.height and
        // childrenRect unions y — height ← childrenRect ← y ← height.
        // _slot.height below is loop-free (no child's HEIGHT depends on
        // this item), and the row height reads THAT instead of a second
        // childrenRect binding.
        implicitHeight: Math.max(_labels.implicitHeight, _slot.height) + 16

        Column {
            id: _labels
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: prow.labelText
                color: Theme.textColor
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                visible: prow.descText.length > 0
                text: prow.descText
                color: root._faintColor
                font.pixelSize: 11
            }
        }
        Item {
            id: _slot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            // Max of the children's own heights (not their union rect —
            // see the loop note above). Identical value for this page's
            // slots: every slot child is either at y=0 or v-centered, so
            // the union height already equals the tallest child.
            height: {
                let h = 0
                for (let i = 0; i < children.length; ++i)
                    h = Math.max(h, children[i].height)
                return h
            }
        }
    }
}
