import QtQuick
import QtQuick.Controls
import DesktopPet

// Instance detail page -- Phase 5 todo 6.
//
// Full per-instance control UI per architecture-blueprint.md §4.1.3. Binds to
// an InstanceSession via the `instance` property (set by Main.qml / todo 7
// Sidebar). Renders an empty state when instance is null so the page is
// verifiable before the sidebar wiring lands.
//
// All colors bind to the Theme singleton (Catppuccin Mocha base palette) so
// the page re-renders instantly on theme switches -- no hardcoded colors.
// Visual language mirrors WelcomePage.qml: Flickable + 32px outer margins +
// surfaceColor panels (10px radius) + section spacing + muted text derived
// from Theme.textColor at 0.6 alpha.
//
// Phase-8 placeholders (voice pack ComboBox, subtitle controls, layout
// controls) are deliberately stubbed -- todos 19/21/22 implement them.
Rectangle {
    id: root
    color: Theme.bgColor

    // The InstanceSession* this page renders. null until todo 7 wires Sidebar
    // selection -> Main.qml -> this property. Every binding guards on
    // `instance != null` so the page renders cleanly in the pre-wiring state.
    // Nav-url loading passes no initial properties; self-select the first
    // instance (same pattern as MonitorPage).
    property var instance: instanceManager.instanceAt
                           ? instanceManager.instanceAt(0) : null

    // Emitted when the user clicks Delete. Main.qml / todo 7 connects this to
    // the confirm-dialog flow (blueprint §4.2: default-focus Cancel). The page
    // does NOT call InstanceManager.deleteInstance directly -- the two-step
    // confirm lives one level up.
    signal deleteRequested()

    // Semantic status colors from Theme (green/amber/gray/red are universal
    // state signals, matching WelcomePage's convention).
    readonly property color _runningColor:    Theme.successColor
    readonly property color _connectingColor: Theme.warningColor
    readonly property color _stoppedColor:    Theme.mutedTextColor
    readonly property color _errorColor:      Theme.errorColor
    readonly property color _mutedColor: Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)

    // status string -> badge color (mirrors Sidebar's convention + pending/
    // connecting/error InstanceSession emits).
    function _statusColor(s) {
        if (s === "running")                       return _runningColor
        if (s === "connecting" || s === "pending") return _connectingColor
        if (s === "error")                         return _errorColor
        return _stoppedColor
    }
    function _statusText()  { return instance ? instance.status : "stopped" }

    // ── Empty state (pre-todo-7 wiring) ─────────────────────────────────────
    Text {
        anchors.centerIn: parent
        visible: instance === null
        text: qsTr("No instance yet — create one from the Home page")
        color: _mutedColor
        font.pixelSize: 14
    }

    // ── Detail content ──────────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors.fill: parent
        visible: instance !== null
        contentWidth: width
        contentHeight: detailColumn.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: detailColumn
            anchors.left: parent.left
            anchors.leftMargin: 32
            anchors.right: parent.right
            anchors.rightMargin: 32
            anchors.top: parent.top
            anchors.topMargin: 24
            spacing: 20

            // ══════════════════ Header ══════════════════
            PanelCard {
                width: parent.width
                Row {
                    id: headerRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 20
                    spacing: 12

                    TextField {
                        id: labelField
                        width: headerRow.width - statusPill.width
                            - connDot.width - headerRow.spacing * 2
                        text: ""
                        color: Theme.textColor
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        selectByMouse: true
                        background: Rectangle {
                            color: labelField.activeFocus
                                ? Theme.hoverColor
                                : "transparent"
                            radius: 6
                            border.color: labelField.activeFocus
                                ? Theme.accentColor : "transparent"
                            border.width: 1
                        }
                        onEditingFinished: {
                            // Persistence deferred to todo 7 (InstanceManager
                            // observes label edits and writes the instance
                            // file). For Phase 5 we just log.
                            if (instance)
                                console.log("label rename ->", labelField.text,
                                            "(persistence wired in todo 7)")
                        }
                    }
                    Rectangle {
                        id: statusPill
                        anchors.verticalCenter: labelField.verticalCenter
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
                        id: connDot
                        anchors.verticalCenter: labelField.verticalCenter
                        width: 10; height: 10; radius: 5
                        color: (instance && instance.connected)
                            ? _runningColor : _faintColor
                    }
                }
                Row {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: headerRow.bottom
                    anchors.topMargin: 16
                    anchors.margins: 20
                    spacing: 10

                    LifecycleButton {
                        label: qsTr("Start"); emphasis: true
                        enabled: instance && instance.status !== "running"
                        onClicked: instance.start()
                    }
                    LifecycleButton {
                        label: qsTr("Stop")
                        enabled: instance && instance.status === "running"
                        onClicked: instance.stop()
                    }
                    LifecycleButton {
                        label: qsTr("Restart")
                        enabled: instance && instance.status === "running"
                        onClicked: instance.restart()
                    }
                    LifecycleButton {
                        label: qsTr("Delete"); danger: true
                        enabled: instance !== null
                        onClicked: if (instance) instanceManager.requestDelete(instance.instanceId)
                    }
                }
            }

            // ══════════════════ Model section ══════════════════
            PanelCard {
                width: parent.width
                SectionTitle { text: qsTr("Model") }

                ComboBox {
                    id: modelCombo
                    width: parent.width - 40
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    // availableModels wraps core::scanAvailableModels over
                    // the renderer dir -- empty before start() resolves one.
                    model: instance ? instance.availableModels() : []
                    onActivated: if (instance && currentText)
                        instance.loadModel(currentText)
                }

                Text {
                    id: motionsLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.top: modelCombo.bottom
                    anchors.topMargin: 16
                    text: qsTr("Motions")
                    color: _mutedColor
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    visible: instance && instance.modelLoaded
                }
                Grid {
                    id: motionGrid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.top: motionsLabel.bottom
                    anchors.topMargin: 8
                    columns: 4
                    spacing: 8
                    visible: instance && instance.modelLoaded
                    Repeater {
                        // Re-evaluates when modelLoaded flips so the grid
                        // populates on the model_loaded event.
                        model: (instance && instance.modelLoaded)
                            ? instance.motionGroupNames() : []
                        delegate: MotionButton {
                            width: (motionGrid.width - 24) / 4
                            groupName: modelData
                            count: instance.motionCount(modelData)
                            onClicked: instance.playMotion(modelData, 0)
                        }
                    }
                }
                Text {
                    id: noModelHint
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.top: modelCombo.bottom
                    anchors.topMargin: 16
                    text: (instance && instance.modelLoaded)
                        ? qsTr("(no motions defined by this model)")
                        : qsTr("No model loaded -- start the instance to load.")
                    color: _mutedColor
                    font.pixelSize: 12
                    visible: !(instance && instance.modelLoaded
                        && instance.motionGroupNames().length > 0)
                }

                Text {
                    id: expressionsLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 20
                    anchors.top: motionGrid.visible ? motionGrid.bottom
                                                    : noModelHint.top
                    anchors.topMargin: 16
                    text: qsTr("Expressions")
                    color: _mutedColor
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    visible: instance && instance.modelLoaded
                        && instance.expressionNames().length > 0
                }
                Flow {
                    id: expressionRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.top: expressionsLabel.bottom
                    anchors.topMargin: 8
                    spacing: 8
                    visible: expressionsLabel.visible
                    Repeater {
                        model: (instance && instance.modelLoaded)
                            ? instance.expressionNames() : []
                        delegate: ExpressionButton {
                            name: modelData
                            onClicked: instance.setExpression(modelData)
                        }
                    }
                }
            }

            // ══════════════════ Parameter panel ══════════════════
            PanelCard {
                width: parent.width
                SectionTitle { text: qsTr("Parameters") }

                Column {
                    id: paramCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.top: parent.top
                    anchors.topMargin: 50    // clear SectionTitle
                    spacing: 14

                    ParamSlider {
                        width: parent.width
                        labelText: qsTr("Window Opacity")
                        valueText: (instance ? instance.opacity
                                             : 1.0).toFixed(2)
                        from: 0.1; to: 1.0; stepSize: 0.05
                        bindValue: instance ? instance.opacity : 1.0
                        onMoved: if (instance) instance.setOpacity(value)
                    }
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("FPS Mode")
                        SegmentedToggle {
                            anchors.right: parent.right
                            leftLabel: qsTr("Adaptive")
                            rightLabel: qsTr("Fixed")
                            leftActive: instance && instance.targetFps === 0
                            onLeftClicked:  if (instance) instance.setFps(0)
                            onRightClicked: if (instance && instance.targetFps === 0)
                                instance.setFps(30)
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
                        valueText: (instance ? instance.volume
                                             : 1.0).toFixed(2)
                        from: 0.0; to: 1.0; stepSize: 0.05
                        bindValue: instance ? instance.volume : 1.0
                        onMoved: if (instance) instance.setVolume(value)
                    }
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Muted")
                        CheckBox {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: instance ? instance.muted : false
                            onToggled: if (instance)
                                instance.setMuted(checked)
                        }
                    }
                    // dragMode / idleInterval / autoStart -- display-only until
                    // todo 7+ wires the persist path. Read via Q_INVOKABLE
                    // accessors on InstanceSession.
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Drag Mode")
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: instance ? instance.dragMode() : "direct"
                            color: _mutedColor
                            font.pixelSize: 12
                        }
                    }
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Idle Interval (s)")
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: instance ? instance.idleIntervalSeconds() : 10
                            color: _mutedColor
                            font.pixelSize: 12
                        }
                    }
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Auto-start with panel")
                        CheckBox {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: instance ? instance.autoStartEnabled() : false
                            enabled: false  // display-only
                        }
                    }
                }
            }

            // ══════════════════ Subtitle panel (todo 21) ══════════════════
            PanelCard {
                width: parent.width
                SectionTitle { text: qsTr("Subtitle") }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 50
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 14

                    // Preset ComboBox -- 15 Chinese display names from
                    // SubtitlePresets::presetNames (ported from Java
                    // mapPresetToStyle). On activate → setSubtitleStylePreset
                    // sends set_subtitle_style with the preset's style +
                    // persists the preset name.
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Style Preset")
                        ComboBox {
                            id: subtitlePresetCombo
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 220
                            // availableModels() pattern: Q_INVOKABLE returns
                            // the QStringList fresh on each access.
                            model: instance ? instance.subtitlePresetNames() : []
                            onActivated: if (instance && currentText)
                                instance.setSubtitleStylePreset(currentText)
                        }
                    }

                    // Auto-adjust toggle (§D.4 set_subtitle_adjust_mode).
                    // When enabled, the renderer rescales font_size to fit
                    // area_width/area_height; the resulting layout is mirrored
                    // back via subtitle_layout_changed → InstanceSession
                    // persists it for the next launch.
                    ParamRow {
                        width: parent.width
                        labelText: qsTr("Auto-adjust to area")
                        CheckBox {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: instance ? instance.subtitleAdjustModeEnabled() : false
                            onToggled: if (instance)
                                instance.setSubtitleAdjustMode(checked)
                        }
                    }
                }
            }

            // ══════════════════ Layout panel (todo 22) ══════════════════
            PanelCard {
                width: parent.width
                SectionTitle { text: qsTr("Layout") }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 50
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 14

                    // User layout (Shift+drag / Shift+scroll inside the pet
                    // window) persists automatically via layout_changed →
                    // InstanceConfigManager. window_resized (Ctrl+scroll) is
                    // persisted the same way. This panel offers a one-click
                    // reset to defaults.
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: _mutedColor
                        font.pixelSize: 12
                        text: qsTr("Adjust position/scale with Shift+drag or " +
                                   "Shift+scroll inside the pet window; resize " +
                                   "the window with Ctrl+scroll. Changes persist " +
                                   "automatically.")
                    }
                    LifecycleButton {
                        label: qsTr("Reset Layout")
                        enabled: instance && instance.modelLoaded
                        onClicked: if (instance) instance.resetLayout()
                    }
                }
            }

            // ══════════════════ Phase-8 placeholders ══════════════════
            PanelCard {
                width: parent.width
                SectionTitle { text: qsTr("Coming in Phase 8"); faint: true }
                Column {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 50
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 6
                    Phase8Row { width: parent.width; label: qsTr("Voice Pack"); hint: qsTr("todo 19") }
                }
            }

            // ══════════════════ Log panel ══════════════════
            // Instance-scoped ListView (max 50 rows) fed by InstanceSession::
            // commandSent. A full spdlog bridge is out of Phase-5 scope; the
            // command-sent stream is the practical per-instance log.
            PanelCard {
                width: parent.width
                height: 280
                SectionTitle { id: logTitle; text: qsTr("Command Log") }

                Rectangle {
                    id: clearBtn
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.rightMargin: 20
                    anchors.topMargin: 16
                    width: clearText.implicitWidth + 18
                    height: 24
                    radius: 4
                    color: clearArea.containsMouse
                        ? Theme.hoverColor
                        : "transparent"
                    border.color: _mutedColor
                    border.width: 1
                    Text {
                        id: clearText
                        anchors.centerIn: parent
                        text: qsTr("Clear")
                        color: _mutedColor
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: clearArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: logModel.clear()
                    }
                }

                ListView {
                    id: logView
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: logTitle.bottom
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 8
                    anchors.bottomMargin: 20
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
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // ── Instance-scoped log buffer (max 50 rows) ─────────────────────────────
    ListModel { id: logModel }

    // Append every emitted command action (salvo + setters + play_motion /
    // set_expression + shutdown) with a HH:MM:SS timestamp. Cap at 50: drop
    // oldest. Connected only while instance is non-null.
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

    // Refresh the editable label + ComboBox selections whenever the bound
    // instance changes (todo 7 switches instances). QML breaks the text
    // binding on user input so we explicitly rewrite these on swap.
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

    // ── Inline components (Qt 6.3+ `component` syntax, like WelcomePage) ─────

    // Surface-colored card with 10px radius matching WelcomePage's env panel.
    // Height tracks childrenRect (anchored children).
    component PanelCard : Rectangle {
        radius: 10
        color: Theme.surfaceColor
        height: childrenRect.height + 24
    }

    // Section heading inside a card. `faint` dimples it for Phase-8.
    component SectionTitle : Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 20
        anchors.topMargin: 16
        color: faint ? _faintColor : Theme.textColor
        font.pixelSize: 15
        font.weight: Font.DemiBold
        property bool faint: false
    }

    // Lifecycle button (Start/Stop/Restart/Delete). emphasis -> accent fill,
    // danger -> red-tinted, default -> surface outline. Standard idiom: the
    // MouseArea's onClicked emits the root signal; consumers use onClicked.
    component LifecycleButton : Rectangle {
        id: btn
        property string label: ""
        property bool emphasis: false
        property bool danger: false
        property bool enabled: true
        signal clicked()
        width: btnText.implicitWidth + 24
        height: 30
        radius: 6
        opacity: enabled ? 1.0 : 0.4
        color: !enabled        ? "transparent"
             : emphasis        ? Theme.accentColor
             : danger          ? Qt.rgba(_errorColor.r, _errorColor.g,
                                          _errorColor.b, 0.18)
             : hover.containsMouse ? Theme.hoverColor
             :                     "transparent"
        border.color: danger ? _errorColor : _mutedColor
        border.width: 1
        Text {
            id: btnText
            anchors.centerIn: parent
            text: btn.label
            color: emphasis ? Theme.bgColor
                 : danger  ? _errorColor
                 :           Theme.textColor
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: btn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            enabled: btn.enabled
            onClicked: if (btn.enabled) btn.clicked()
        }
    }

    // Motion grid cell: group name (left) + count badge (right).
    component MotionButton : Rectangle {
        id: mb
        property string groupName: ""
        property int count: 0
        signal clicked()
        height: 38
        radius: 6
        color: mArea.containsMouse
            ? Theme.hoverColor
            : Theme.bgColor
        border.color: _faintColor
        border.width: 1
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: mb.groupName
            color: Theme.textColor
            font.pixelSize: 12
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: countText.implicitWidth + 10
            height: 18
            radius: 9
            color: Qt.rgba(Theme.accentColor.r, Theme.accentColor.g,
                           Theme.accentColor.b, 0.25)
            Text {
                id: countText
                anchors.centerIn: parent
                text: mb.count
                color: Theme.accentColor
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
        }
        MouseArea {
            id: mArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: mb.clicked()
        }
    }

    // Expression pill button.
    component ExpressionButton : Rectangle {
        id: eb
        property string name: ""
        signal clicked()
        width: exprText.implicitWidth + 22
        height: 28
        radius: 14
        color: eArea.containsMouse
            ? Theme.accentColor
            : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g,
                      Theme.accentColor.b, 0.20)
        Text {
            id: exprText
            anchors.centerIn: parent
            text: eb.name
            color: eArea.containsMouse ? "#ffffff" : Theme.textColor
            font.pixelSize: 11
        }
        MouseArea {
            id: eArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: eb.clicked()
        }
    }

    // Labeled slider row with value readout. The QML Slider's `value` is bound
    // one-way to `bindValue` (instance -> slider); onMoved pushes back to the
    // instance via the consumer's onMoved handler.
    component ParamSlider : Column {
        property string labelText: ""
        property string valueText: ""
        property real from: 0.0
        property real to: 1.0
        property real stepSize: 0.1
        property real bindValue: 0.0
        property bool sliderEnabled: true
        signal moved()
        spacing: 4
        ParamRow {
            width: parent.width
            labelText: parent.labelText
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: parent.parent.valueText
                color: Theme.accentColor
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
        }
        Slider {
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
        height: 26
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: parent.labelText
            color: Theme.textColor
            font.pixelSize: 12
        }
    }

    // Two-button segmented toggle (adaptive/fixed, dragMode direct/physics).
    component SegmentedToggle : Item {
        id: seg
        property string leftLabel: ""
        property string rightLabel: ""
        property bool leftActive: true
        signal leftClicked()
        signal rightClicked()
        width: segLeft.width + segRight.width - 1
        height: 26
        Rectangle {
            id: segLeft
            anchors.left: parent.left
            width: slText.implicitWidth + 22
            height: parent.height
            radius: 4
            color: seg.leftActive ? Theme.accentColor : "transparent"
            border.color: _mutedColor; border.width: 1
            Text {
                id: slText
                anchors.centerIn: parent
                text: seg.leftLabel
                color: seg.leftActive ? Theme.bgColor : _mutedColor
                font.pixelSize: 11; font.weight: Font.Medium
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: seg.leftClicked()
            }
        }
        Rectangle {
            id: segRight
            anchors.left: segLeft.right
            anchors.leftMargin: -1   // share border
            width: srText.implicitWidth + 22
            height: parent.height
            radius: 4
            color: !seg.leftActive ? Theme.accentColor : "transparent"
            border.color: _mutedColor; border.width: 1
            Text {
                id: srText
                anchors.centerIn: parent
                text: seg.rightLabel
                color: !seg.leftActive ? Theme.bgColor : _mutedColor
                font.pixelSize: 11; font.weight: Font.Medium
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: seg.rightClicked()
            }
        }
    }

    // Phase-8 placeholder row: label + "todo N" hint, dimmed.
    component Phase8Row : Item {
        property string label: ""
        property string hint: ""
        height: 28
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: _faintColor
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: parent.hint
            color: _faintColor
            font.pixelSize: 11
            font.italic: true
        }
    }
}
