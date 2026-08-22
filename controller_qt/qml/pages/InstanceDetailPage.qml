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

    CreateInstanceDialog {
        id: createDialog
        onAccepted: (name, avatar) => {
            const uuid = instanceManager.createInstance(name, avatar)
            if (uuid !== "") {
                const row = instanceManager.rowCount() - 1
                Window.window.selectInstance(
                    row, instanceManager.instanceAt(row).instanceId)
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
                            color: Theme.accentAlpha(0.1)
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
                            style: "danger"
                            text: qsTr("🗑 删除")
                            anchors.verticalCenter: parent.verticalCenter
                            // requestDelete expects the uuid STRING; passing
                            // instance.instanceId (an int routing id) never
                            // matched rowForUuid — delete silently no-op'd.
                            onClicked: if (instance)
                                instanceManager.requestDelete(instance.uuid)
                        }
                        AppButton {
                            text: qsTr("↻ 重启")
                            visible: instance && instance.status === "running"
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: instance.restart()
                        }
                        AppButton {
                            text: qsTr("⏹ 停止")
                            visible: instance && instance.status === "running"
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: instance.stop()
                        }
                        AppButton {
                            style: "primary"
                            text: qsTr("▶ 启动")
                            visible: !(instance && instance.status === "running")
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: instance.start()
                        }
                    }
                }
            }

            // ══════════════ Two-column zone ════════════════════════════
            Row {
                width: parent.width
                spacing: Theme.spaceGroup

                // ── LEFT column ───────────────────────────────────────
                Column {
                    width: root._wide
                        ? (parent.width - Theme.spaceGroup) * 0.6
                        : parent.width
                    spacing: Theme.spaceGroup

                    // Stage card: model + 3 tabs
                    Card {
                        width: parent.width
                        title: qsTr("舞台 · 模型与动作")
                        hint: qsTr("模型切换即时生效 (load_model)")

                        Row {
                            width: parent.width
                            spacing: 8
                            AppComboBox {
                                id: modelCombo
                                width: parent.width - 90
                                model: instance ? instance.availableModels() : []
                                onActivated: if (instance && currentText)
                                    instance.loadModel(currentText)
                            }
                            AppButton {
                                style: "primary"
                                text: qsTr("应用")
                                width: 78
                                onClicked: if (instance && modelCombo.currentText)
                                    instance.loadModel(modelCombo.currentText)
                            }
                        }
                        Text {
                            visible: !(instance && instance.modelLoaded)
                                     && instance && instance.availableModels().length === 0
                            text: qsTr("未找到模型 — 请检查渲染器旁的 Resources/Models/")
                            color: root._faintColor
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
                                    onActivated: instance.playMotion(modelData, 0)
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
                                    onActivated: instance.setExpression(modelData)
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
                                    onActivated: instance.playMotion(modelData, 0)
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
                            text: qsTr("命中区域来自 set_hit_areas 缓存，点击可试触 (play_motion)")
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
                            descText: qsTr("0.10 – 1.00")
                            valueText: (instance ? instance.opacity : 1.0).toFixed(2)
                            from: 0.1; to: 1.0; stepSize: 0.05
                            bindValue: instance ? instance.opacity : 1.0
                            onMoved: (val) => { if (instance) instance.setOpacity(val) }
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
                                    onMoved: (val) => { if (instance) instance.setVolume(val) }
                                }
                                Text {
                                    text: (instance ? instance.volume : 1.0).toFixed(2)
                                    color: Theme.accentColor
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                ToggleSwitch {
                                    checked: instance ? instance.muted : false
                                    anchors.verticalCenter: parent.verticalCenter
                                    onToggled: if (instance)
                                        instance.setMuted(checked)
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
                                        instance.setFps(60)
                                    else if (v === qsTr("自适应"))
                                        instance.setFps(0)
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
                                if (instance) instance.setFps(Math.round(val))
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("待机动作间隔")
                            descText: qsTr("Scheduler 空闲轮询")
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
                            descText: qsTr("drag_persist 已启用")
                            Text {
                                text: instance ? instance.dragMode() : "direct"
                                color: root._mutedColor
                                font.pixelSize: 13
                            }
                        }
                    }
                }

                // ── RIGHT column ───────────────────────────────────────
                Column {
                    visible: root._wide
                    width: root._wide
                        ? (parent.width - Theme.spaceGroup) * 0.4
                        : parent.width
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("字幕样式")

                        AppComboBox {
                            id: subtitlePresetCombo
                            width: parent.width
                            model: instance ? instance.subtitlePresetNames() : []
                            onActivated: if (instance && currentText)
                                instance.setSubtitleStylePreset(currentText)
                        }

                        Rectangle {
                            width: parent.width
                            height: 64
                            radius: Theme.radiusMd
                            color: Theme.dark ? "#00000040" : "#0000000d"
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("“今天也要元气满满哦！”")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: "#7a4a00"
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 6
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: subtitlePresetCombo.currentText
                                color: root._faintColor
                                font.pixelSize: 11
                            }
                        }
                        SettingRow {
                            width: parent.width
                            title: qsTr("自动适应区域")
                            desc: qsTr("按窗口尺寸缩放字幕")
                            ToggleSwitch {
                                anchors.verticalCenter: parent.verticalCenter
                                checked: instance
                                    ? instance.subtitleAdjustModeEnabled() : false
                                onToggled: if (instance)
                                    instance.setSubtitleAdjustMode(checked)
                            }
                        }
                    }

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
                                onClicked: if (instance) instance.resetLayout()
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
                }
            }

            // ══════════════ Command log drawer ═════════════════════════
            Card {
                id: logCard
                width: parent.width
                property bool expanded: true

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

    Connections {
        target: root
        function onInstanceChanged() {
            root._syncCombos()
        }
    }
    Component.onCompleted: root._syncCombos()

    function _syncCombos() {
        if (!instance) return
        const i = modelCombo.find(instance.modelName)
        modelCombo.currentIndex = i >= 0 ? i : -1
        const j = subtitlePresetCombo.find(instance.subtitleStylePreset())
        subtitlePresetCombo.currentIndex = j >= 0 ? j : -1
    }

    // ── Inline components ──────────────────────────────────────────────
    component StageTab : Rectangle {
        id: tab
        property string text: ""
        property int tab: 0
        width: tabLabel.implicitWidth + 28
        height: 32
        color: "transparent"
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
        implicitHeight: Math.max(_labels.implicitHeight, _slot.childrenRect.height) + 16

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
            height: childrenRect.height
        }
    }
}
