import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import FluentUI
import DesktopPet

// Instance detail page — Fluent UI redesign (two-column workbench).
//
// Layout (design doc §②): header bar + two columns + collapsible log drawer.
//   LEFT  (1.5): Stage card (model select + pivot tabs: motions/expressions),
//                Runtime parameters card (SettingRow rows).
//   RIGHT (1):   Subtitle card (preset + live preview), Layout card,
//                Voice-pack summary card.
//   BOTTOM:      Command log drawer (direction-colored entries).
//
// All business bindings preserved verbatim from the previous single-column
// version: instance.start/stop/restart/delete, loadModel, playMotion,
// setExpression, setOpacity/setFps/setVolume/setMuted, subtitle presets,
// resetLayout, onCommandSent log stream.
//
// Narrow-window fallback: below 900px the two columns stack vertically.
Rectangle {
    id: root
    color: "transparent"

    property var instance: instanceManager.instanceAt
                           ? instanceManager.instanceAt(0) : null

    signal deleteRequested()

    readonly property color _mutedColor: Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)
    readonly property color _errorColor: Theme.errorColor

    // Breakpoint: stack columns on narrow windows.
    readonly property bool _wide: width >= 900

    function _statusText() { return instance ? instance.status : "stopped" }

    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("No instance yet — create one from the Home page")
        color: _mutedColor
        font: FluTextStyle.Body
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: instance !== null
        contentWidth: width
        contentHeight: detailCol.implicitHeight + 48 + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FluScrollBar {}

        ColumnLayout {
            id: detailCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            y: 24
            spacing: Theme.spaceGroup

            // ══════════════════ Header bar ══════════════════
            FluFrame {
                Layout.fillWidth: true
                padding: Theme.spaceCard

                RowLayout {
                    width: parent.width
                    spacing: 12

                    FluText {
                        text: instance ? instance.label : ""
                        font: FluTextStyle.Subtitle
                        Layout.fillWidth: true
                    }
                    StatusPill {
                        status: root._statusText()
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 10; height: 10; radius: 5
                        color: (instance && instance.connected)
                            ? Theme.successColor : _faintColor
                    }
                    FluButton {
                        text: qsTr("Stop")
                        visible: instance && instance.status === "running"
                        onClicked: instance.stop()
                    }
                    FluButton {
                        text: qsTr("Restart")
                        visible: instance && instance.status === "running"
                        onClicked: instance.restart()
                    }
                    FluFilledButton {
                        text: qsTr("Start")
                        visible: !(instance && instance.status === "running")
                        onClicked: instance.start()
                    }
                    FluButton {
                        text: qsTr("Delete")
                        normalColor: FluTheme.dark
                            ? Qt.rgba(_errorColor.r, _errorColor.g,
                                      _errorColor.b, 0.2)
                            : Qt.rgba(_errorColor.r, _errorColor.g,
                                      _errorColor.b, 0.12)
                        textColor: _errorColor
                        onClicked: if (instance)
                            instanceManager.requestDelete(instance.instanceId)
                    }
                }
            }

            // ══════════════════ Two-column zone ══════════════════
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spaceGroup

                // ── LEFT column ─────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: root._wide ? 1.5 : 1
                    spacing: Theme.spaceGroup

                    // Stage card: model + pivot (motions/expressions)
                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("舞台 · 模型与动作")
                        hint: (instance && instance.modelLoaded)
                              ? qsTr("点击动作立即播放 (play_motion)")
                              : qsTr("选择模型；启动后加载")

                        FluComboBox {
                            id: modelCombo
                            width: parent.width
                            model: instance ? instance.availableModels() : []
                            onActivated: if (instance && currentText)
                                instance.loadModel(currentText)
                        }

                        FluText {
                            width: parent.width
                            visible: !(instance && instance.modelLoaded)
                            text: (instance && instance.availableModels().length === 0)
                                ? qsTr("No models found — check Resources/Models/ " +
                                       "next to the renderer.")
                                : qsTr("Select a model; it loads after Start.")
                            color: root._faintColor
                            font: FluTextStyle.Caption
                        }

                        // Pivot tabs: motions / expressions
                        Column {
                            width: parent.width
                            spacing: 10
                            visible: instance && instance.modelLoaded

                            Row {
                                spacing: 4
                                FluToggleButton {
                                    text: qsTr("动作 %1").arg(
                                        instance ? instance.motionGroupNames().length : 0)
                                    checked: stageTab === 0
                                    onClicked: stageTab = 0
                                }
                                FluToggleButton {
                                    text: qsTr("表情 %1").arg(
                                        instance ? instance.expressionNames().length : 0)
                                    checked: stageTab === 1
                                    onClicked: stageTab = 1
                                }
                            }

                            Flow {
                                width: parent.width
                                spacing: 8
                                visible: stageTab === 0

                                Repeater {
                                    model: (instance && instance.modelLoaded)
                                        ? instance.motionGroupNames() : []
                                    delegate: Chip {
                                        text: modelData + " (" +
                                              instance.motionCount(modelData) + ")"
                                        onActivated:
                                            instance.playMotion(modelData, 0)
                                    }
                                }
                            }
                            FluText {
                                visible: stageTab === 0 && instance
                                         && instance.modelLoaded
                                         && instance.motionGroupNames().length === 0
                                text: qsTr("(no motions defined by this model)")
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }

                            Flow {
                                width: parent.width
                                spacing: 8
                                visible: stageTab === 1

                                Repeater {
                                    model: (instance && instance.modelLoaded)
                                        ? instance.expressionNames() : []
                                    delegate: Chip {
                                        text: modelData
                                        onActivated:
                                            instance.setExpression(modelData)
                                    }
                                }
                            }
                            FluText {
                                visible: stageTab === 1 && instance
                                         && instance.modelLoaded
                                         && instance.expressionNames().length === 0
                                text: qsTr("(no expressions defined by this model)")
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }
                        }

                        property int stageTab: 0
                    }

                    // Runtime parameters card
                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("运行参数")

                        ParamSlider {
                            width: parent.width
                            labelText: qsTr("Window Opacity")
                            valueText: (instance ? instance.opacity : 1.0).toFixed(2)
                            from: 0.1; to: 1.0; stepSize: 0.05
                            bindValue: instance ? instance.opacity : 1.0
                            onMoved: if (instance) instance.setOpacity(value)
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("FPS Mode")
                            Row {
                                spacing: 8
                                FluToggleButton {
                                    text: qsTr("Adaptive")
                                    checked: instance && instance.targetFps === 0
                                    onClicked: if (instance) instance.setFps(0)
                                }
                                FluToggleButton {
                                    text: qsTr("Fixed")
                                    checked: instance && instance.targetFps > 0
                                    onClicked: if (instance && instance.targetFps === 0)
                                        instance.setFps(30)
                                }
                            }
                        }
                        ParamSlider {
                            width: parent.width
                            labelText: qsTr("Target FPS")
                            valueText: (instance && instance.targetFps > 0)
                                ? instance.targetFps : qsTr("adaptive")
                            from: 15; to: 120; stepSize: 1
                            bindValue: instance && instance.targetFps > 0
                                ? instance.targetFps : 30
                            sliderEnabled: instance && instance.targetFps > 0
                            onMoved: if (instance)
                                instance.setFps(Math.round(value))
                        }
                        ParamSlider {
                            width: parent.width
                            labelText: qsTr("Volume")
                            valueText: (instance ? instance.volume : 1.0).toFixed(2)
                            from: 0.0; to: 1.0; stepSize: 0.05
                            bindValue: instance ? instance.volume : 1.0
                            onMoved: if (instance) instance.setVolume(value)
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("Muted")
                            FluToggleSwitch {
                                checked: instance ? instance.muted : false
                                onCheckedChanged: if (instance)
                                    instance.setMuted(checked)
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("Drag Mode")
                            FluText {
                                text: instance ? instance.dragMode() : "direct"
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("Idle Interval (s)")
                            FluText {
                                text: instance ? instance.idleIntervalSeconds() : 10
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }
                        }
                        ParamRow {
                            width: parent.width
                            labelText: qsTr("Auto-start with panel")
                            FluCheckBox {
                                checked: instance ? instance.autoStartEnabled() : false
                                enabled: false  // display-only
                            }
                        }
                    }
                }

                // ── RIGHT column ────────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: Theme.spaceGroup
                    visible: root._wide || true   // stacks when narrow

                    // Subtitle card with live preview
                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("字幕样式")

                        FluComboBox {
                            id: subtitlePresetCombo
                            width: parent.width
                            model: instance ? instance.subtitlePresetNames() : []
                            onActivated: if (instance && currentText)
                                instance.setSubtitleStylePreset(currentText)
                        }

                        // Live preview of the selected preset.
                        Rectangle {
                            width: parent.width
                            height: 64
                            radius: Theme.radiusMd
                            color: FluTheme.dark ? Qt.rgba(0,0,0,0.25)
                                                 : Qt.rgba(0,0,0,0.05)
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("“今天也要元气满满哦！”")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.textColor
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 6
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: subtitlePresetCombo.currentText
                                color: root._faintColor
                                font: FluTextStyle.Caption
                            }
                        }

                        ParamRow {
                            width: parent.width
                            labelText: qsTr("Auto-adjust to area")
                            FluToggleSwitch {
                                checked: instance
                                    ? instance.subtitleAdjustModeEnabled() : false
                                onCheckedChanged: if (instance)
                                    instance.setSubtitleAdjustMode(checked)
                            }
                        }
                    }

                    // Layout card
                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("布局")

                        FluText {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: root._mutedColor
                            font: FluTextStyle.Caption
                            text: qsTr("宠物窗口内 Shift+拖动 移动 · Shift+滚轮 缩放 · " +
                                       "Ctrl+滚轮 调整窗口。变更自动持久化。")
                        }
                        FluButton {
                            text: qsTr("Reset Layout")
                            enabled: instance && instance.modelLoaded
                            onClicked: if (instance) instance.resetLayout()
                        }
                    }

                    // Voice-pack summary card
                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("语音包")

                        FluText {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: root._mutedColor
                            font: FluTextStyle.Caption
                            text: qsTr("语音包的发现、元数据与挂载管理已移至独立的" +
                                       "「语音包」页面。挂载后行为引擎将优先于默认" +
                                       "命中处理。")
                        }
                        FluTextButton {
                            text: qsTr("管理语音包 →")
                            onClicked:
                                Window.window.switchPage("voicepack")
                        }
                    }
                }
            }

            // ══════════════════ Command log drawer ══════════════════
            FluFrame {
                id: logCard
                Layout.fillWidth: true
                padding: Theme.spaceCard

                // Drawer collapse state
                property bool expanded: true

                Column {
                    width: parent.width
                    spacing: 12

                    // Header: anchored layout (Row children cannot use
                    // anchors.right — the pre-fix header silently vanished).
                    Item {
                        width: parent.width
                        height: 36

                        FluText {
                            id: logTitle
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("命令日志")
                            font: FluTextStyle.BodyStrong
                            color: Theme.accentColor
                        }
                        FluText {
                            anchors.left: logTitle.right
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: logCard.expanded
                            text: qsTr("最近 50 条 · 实时")
                            color: root._faintColor
                            font: FluTextStyle.Caption
                        }
                        FluTextButton {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Clear")
                            onClicked: logModel.clear()
                        }
                        FluTextButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 70
                            anchors.verticalCenter: parent.verticalCenter
                            text: logCard.expanded ? qsTr("收起 ▴") : qsTr("展开 ▾")
                            onClicked: logCard.expanded = !logCard.expanded
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: logCard.expanded ? 220 : 0
                        visible: logCard.expanded
                        radius: Theme.radiusSm
                        color: FluTheme.dark ? Qt.rgba(0,0,0,0.3)
                                             : Qt.rgba(0,0,0,0.04)
                        border.color: root._faintColor
                        border.width: 1

                        ListView {
                            id: logView
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            model: logModel
                            delegate: Text {
                                width: logView.width
                                text: "[" + model.time + "] → " + model.action
                                color: Theme.textColor
                                font.pixelSize: 11
                                font.family: "Consolas,Menlo,Mono"
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: logModel.count === 0
                                text: qsTr("(no commands sent yet)")
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }
                        }
                    }
                }
            }
        }
    }

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
            if (instance) {
                const i = modelCombo.find(instance.modelName)
                modelCombo.currentIndex = i >= 0 ? i : -1
                const j = subtitlePresetCombo.find(instance.subtitleStylePreset())
                subtitlePresetCombo.currentIndex = j >= 0 ? j : -1
            }
        }
    }
    Component.onCompleted: {
        if (instance) {
            const i = modelCombo.find(instance.modelName)
            modelCombo.currentIndex = i >= 0 ? i : -1
            const j = subtitlePresetCombo.find(instance.subtitleStylePreset())
            subtitlePresetCombo.currentIndex = j >= 0 ? j : -1
        }
    }

    // ── Inline components ──────────────────────────────────────────────────

    // Labeled slider row with value readout (unchanged contract from the
    // previous page version).
    component ParamSlider : Column {
        id: pslider
        property string labelText: ""
        property string valueText: ""
        property real from: 0.0
        property real to: 1.0
        property real stepSize: 0.1
        property real bindValue: 0.0
        property bool sliderEnabled: true
        signal moved()
        spacing: 8
        ParamRow {
            width: parent.width
            labelText: pslider.labelText
            FluText {
                text: pslider.valueText
                color: Theme.accentColor
                font: FluTextStyle.BodyStrong
            }
        }
        FluSlider {
            width: parent.width
            from: parent.from
            to: parent.to
            stepSize: parent.stepSize
            value: parent.bindValue
            enabled: parent.sliderEnabled
            onMoved: parent.moved()
        }
    }

    // Label-left / control-right parameter row. Consumer-declared children
    // are routed (default property alias) into a right-anchored slot so they
    // never overlap the label — the pre-fix version left them at (0,0).
    component ParamRow : Item {
        id: prow
        property string labelText: ""
        default property alias _content: _slot.data
        height: 30
        FluText {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: prow.labelText
            font: FluTextStyle.Body
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
