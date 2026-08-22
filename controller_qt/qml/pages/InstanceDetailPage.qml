import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import FluentUI
import DesktopPet

// Instance detail page — FluentUI rewrite.
//
// Binds to an InstanceSession via the `instance` property (set by Main.qml).
// All business surfaces preserved: lifecycle buttons, model selection,
// motion/expression triggers, parameter setters, subtitle presets, layout
// reset, command log. Visuals are FluentUI: FluFrame cards, FluText
// typography, FluButton/FluFilledButton, FluSlider, FluCheckBox,
// FluToggleSwitch, FluComboBox.
Rectangle {
    id: root
    color: "transparent"

    property var instance: instanceManager.instanceAt
                           ? instanceManager.instanceAt(0) : null

    signal deleteRequested()

    readonly property color _runningColor:    Theme.successColor
    readonly property color _connectingColor: Theme.warningColor
    readonly property color _stoppedColor:    Theme.mutedTextColor
    readonly property color _errorColor:      Theme.errorColor
    readonly property color _mutedColor:      Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)

    function _statusColor(s) {
        if (s === "running")                       return _runningColor
        if (s === "connecting" || s === "pending") return _connectingColor
        if (s === "error")                         return _errorColor
        return _stoppedColor
    }
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
        contentHeight: detailColumn.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: detailColumn
            width: root.width - 64
            x: 32
            spacing: 20
            topPadding: 24

            // ══════════════════ Header ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 16

                    RowLayout {
                        width: parent.width
                        spacing: 12

                        FluTextBox {
                            id: labelField
                            Layout.fillWidth: true
                            text: ""
                            font: FluTextStyle.Subtitle
                            onEditingFinished: if (instance)
                                console.log("label rename ->", labelField.text)
                        }
                        Rectangle {
                            id: statusPill
                            Layout.alignment: Qt.AlignVCenter
                            width: statusRow.implicitWidth + 20
                            height: 26
                            radius: 13
                            readonly property color c: root._statusColor(
                                root._statusText())
                            color: Qt.rgba(c.r, c.g, c.b, 0.18)
                            Row {
                                id: statusRow
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: parent.parent.c
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root._statusText()
                                    color: parent.parent.c
                                    font.pixelSize: 11
                                    font.capitalization: Font.AllUppercase
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            width: 10; height: 10; radius: 5
                            color: (instance && instance.connected)
                                ? _runningColor : _faintColor
                        }
                    }

                    Row {
                        spacing: 10

                        FluFilledButton {
                            text: qsTr("Start")
                            enabled: instance && instance.status !== "running"
                            onClicked: instance.start()
                        }
                        FluButton {
                            text: qsTr("Stop")
                            enabled: instance && instance.status === "running"
                            onClicked: instance.stop()
                        }
                        FluButton {
                            text: qsTr("Restart")
                            enabled: instance && instance.status === "running"
                            onClicked: instance.restart()
                        }
                        FluButton {
                            text: qsTr("Delete")
                            normalColor: FluTheme.dark
                                ? Qt.rgba(_errorColor.r, _errorColor.g,
                                          _errorColor.b, 0.2)
                                : Qt.rgba(_errorColor.r, _errorColor.g,
                                          _errorColor.b, 0.12)
                            textColor: _errorColor
                            enabled: instance !== null
                            onClicked: if (instance)
                                instanceManager.requestDelete(instance.instanceId)
                        }
                    }
                }
            }

            // ══════════════════ Model section ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 12

                    FluText {
                        text: qsTr("Model")
                        font: FluTextStyle.BodyStrong
                        color: Theme.accentColor
                    }

                    FluComboBox {
                        id: modelCombo
                        width: parent.width
                        // availableModels wraps core::scanAvailableModels over
                        // the renderer dir -- empty before start() resolves one.
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
                        color: _faintColor
                        font: FluTextStyle.Caption
                    }

                    FluText {
                        visible: instance && instance.modelLoaded
                        text: qsTr("Motions")
                        font: FluTextStyle.BodyStrong
                        color: _mutedColor
                    }
                    Grid {
                        id: motionGrid
                        visible: instance && instance.modelLoaded
                        columns: 4
                        spacing: 8
                        width: parent.width
                        Repeater {
                            model: (instance && instance.modelLoaded)
                                ? instance.motionGroupNames() : []
                            delegate: FluButton {
                                width: (motionGrid.width - 24) / 4
                                text: modelData + " (" +
                                      instance.motionCount(modelData) + ")"
                                onClicked: instance.playMotion(modelData, 0)
                            }
                        }
                    }
                    FluText {
                        visible: instance && instance.modelLoaded
                                  && instance.motionGroupNames().length === 0
                        text: qsTr("(no motions defined by this model)")
                        color: _mutedColor
                        font: FluTextStyle.Caption
                    }

                    Flow {
                        visible: instance && instance.modelLoaded
                                  && instance.expressionNames().length > 0
                        spacing: 8
                        width: parent.width
                        FluText {
                            visible: false
                        }
                        Repeater {
                            model: (instance && instance.modelLoaded)
                                ? instance.expressionNames() : []
                            delegate: FluToggleButton {
                                text: modelData
                                onClicked: instance.setExpression(modelData)
                            }
                        }
                    }
                }
            }

            // ══════════════════ Parameter panel ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 18

                    FluText {
                        text: qsTr("Parameters")
                        font: FluTextStyle.BodyStrong
                        color: Theme.accentColor
                    }

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
                            color: _mutedColor
                            font: FluTextStyle.Caption
                        }
                    }
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Idle Interval (s)")
                        FluText {
                            text: instance ? instance.idleIntervalSeconds() : 10
                            color: _mutedColor
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

            // ══════════════════ Subtitle panel ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 18

                    FluText {
                        text: qsTr("Subtitle")
                        font: FluTextStyle.BodyStrong
                        color: Theme.accentColor
                    }

                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Style Preset")
                        FluComboBox {
                            id: subtitlePresetCombo
                            width: 220
                            // Q_INVOKABLE returns the QStringList fresh on
                            // each access.
                            model: instance ? instance.subtitlePresetNames() : []
                            onActivated: if (instance && currentText)
                                instance.setSubtitleStylePreset(currentText)
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
            }

            // ══════════════════ Layout panel ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 14

                    FluText {
                        text: qsTr("Layout")
                        font: FluTextStyle.BodyStrong
                        color: Theme.accentColor
                    }
                    FluText {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: _mutedColor
                        font: FluTextStyle.Caption
                        text: qsTr("Adjust position/scale with Shift+drag or " +
                                   "Shift+scroll inside the pet window; resize " +
                                   "the window with Ctrl+scroll. Changes " +
                                   "persist automatically.")
                    }
                    FluButton {
                        text: qsTr("Reset Layout")
                        enabled: instance && instance.modelLoaded
                        onClicked: if (instance) instance.resetLayout()
                    }
                }
            }

            // ══════════════════ Command log ══════════════════
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 12

                    Row {
                        width: parent.width
                        FluText {
                            text: qsTr("Command Log")
                            font: FluTextStyle.BodyStrong
                            color: Theme.accentColor
                        }
                        FluTextButton {
                            anchors.right: parent.right
                            text: qsTr("Clear")
                            onClicked: logModel.clear()
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 240
                        radius: 4
                        color: FluTheme.dark ? Qt.rgba(0,0,0,0.25)
                                             : Qt.rgba(0,0,0,0.04)
                        border.color: _faintColor
                        border.width: 1

                        ListView {
                            id: logView
                            anchors.fill: parent
                            anchors.margins: 8
                            clip: true
                            model: logModel
                            delegate: Text {
                                width: logView.width
                                text: "[" + model.time + "] " + model.action
                                color: Theme.textColor
                                font.pixelSize: 11
                                font.family: "Consolas,Menlo,Mono"
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: logModel.count === 0
                                text: qsTr("(no commands sent yet)")
                                color: _mutedColor
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
            labelField.text = instance ? instance.label : ""
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
            labelField.text = instance.label
            const i = modelCombo.find(instance.modelName)
            modelCombo.currentIndex = i >= 0 ? i : -1
            const j = subtitlePresetCombo.find(instance.subtitleStylePreset())
            subtitlePresetCombo.currentIndex = j >= 0 ? j : -1
        }
    }

    // ── Inline components ──────────────────────────────────────────────────

    // Labeled slider row with value readout. `value` is bound one-way to
    // bindValue (instance -> slider); onMoved pushes back to the instance
    // via the consumer's onMoved handler.
    component ParamSlider : Column {
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
            labelText: parent.labelText
            FluText {
                text: parent.parent.valueText
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

    // Label-left / arbitrary-right parameter row.
    component ParamRow : Item {
        property string labelText: ""
        height: 30
        FluText {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: parent.labelText
            font: FluTextStyle.Body
        }
    }
}
