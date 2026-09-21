import QtQuick
import QtQuick.Controls
import DesktopPet

// 模型库 page — 1:1 port of the approved mock
// (/tmp/opencode/model-library-design/model-library.html).
//
// Layout: header (title + open-dir/rescan pair) → empty-state Card →
// 58/42 two-column zone. Left: 64px model rows (hash-gradient initial
// blocks, "N 组动作 · M 个表情 · K 个命中区域", 使用中 pill) + a detail
// card (motion chips with trial-play, read-only expressions, read-only
// hit areas + jump-to-instance). Right: per-instance model AppComboBox
// (Segmented cannot hold 8 options; matches the stage-card combo) +
// new-instance default model.
//
// Failure path (mock state ③): before switching, the row records the
// previous model. A modelLoadFailureRevision bump carrying a non-empty
// lastModelLoadError bounces the row's combo back to the previous model
// and raises an AppDialog. Offline instances: loadModel() writes config
// only (backend contract), the row desc says "下次启动时生效".
//
// All modelLibrary Q_INVOKABLE reads sit in bindings that reference
// modelLibrary.revision (same refresh-counter pattern as mountsRevision
// in VoicePackPage).
Rectangle {
    id: root
    color: "transparent"

    signal requestSwitchPage(string name)

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color

    property string selectedModel: ""

    // Every model dir name — re-derived whenever the revision counter bumps.
    readonly property var allNames: {
        modelLibrary.revision
        const out = []
        for (let i = 0; i < modelLibrary.modelCount; ++i)
            out.push(modelLibrary.modelDirName(i))
        return out
    }

    // ── Helpers ────────────────────────────────────────────────────────

    // Instances whose configured modelName equals `name` ("使用中 ×N").
    // Reads rowCount + each session's modelName, so the binding re-evaluates
    // on roster changes and per-session modelName notifications.
    function _usageCount(name) {
        let c = 0
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && s.modelName === name)
                ++c
        }
        return c
    }

    // First RUNNING instance currently on `name` — the motion-chip trial
    // target. -1 disables trial-play (chips go 45% dim, mock footnote).
    function _trialRowFor(name) {
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && s.status === "running" && s.modelName === name)
                return i
        }
        return -1
    }

    // Deterministic gradient pair per model name (mock: hash-picked from
    // 8 preset pairs so every model keeps a stable, distinct color).
    function _gradFor(name) {
        const pairs = [
            ["#4cc2ff", "#2980d6"], ["#eaa300", "#f07f3c"],
            ["#b79df5", "#7e57d6"], ["#2fb89a", "#0f7b6c"],
            ["#f472d0", "#c04a98"], ["#6fbf8a", "#2f8f5a"],
            ["#f59a8a", "#d65f4a"], ["#9aa8b8", "#5c6a7a"]
        ]
        let h = 0
        for (let i = 0; i < name.length; ++i)
            h = (h + name.charCodeAt(i)) % pairs.length
        return pairs[h]
    }

    function _statLine(groupCount, exprCount, hitCount) {
        return qsTr("%1 组动作 · %2 · %3")
            .arg(groupCount)
            .arg(exprCount > 0 ? qsTr("%1 个表情").arg(exprCount) : qsTr("无表情"))
            .arg(hitCount > 0 ? qsTr("%1 个命中区域").arg(hitCount) : qsTr("无命中区域"))
    }

    function notifyModelLoadFailed(instanceLabel, failedModel, restoredModel, error) {
        failDialog.message = qsTr("为「%1」切换模型到「%2」失败，已回弹为原模型「%3」，实例保持运行，不受影响。\n%4")
            .arg(instanceLabel).arg(failedModel).arg(restoredModel).arg(error)
        failDialog.open()
    }

    Component.onCompleted: {
        if (root.selectedModel === "" && modelLibrary.modelCount > 0)
            root.selectedModel = modelLibrary.modelDirName(0)
    }

    // Keep the detail card pointing at an existing model after a rescan
    // removed the previously selected directory.
    Connections {
        target: modelLibrary
        function onRevisionChanged() {
            if (root.allNames.indexOf(root.selectedModel) < 0
                    && modelLibrary.modelCount > 0)
                root.selectedModel = modelLibrary.modelDirName(0)
        }
    }

    // Per-instance model row — mirrors SettingRow's geometry (title 13px
    // DemiBold + desc 11px left, control right-anchored) but with a
    // StatusPill next to the title, which SettingRow's string-only title
    // cannot host.
    component InstanceModelRow: Item {
        id: irow

        required property int index
        required property string label

        readonly property QtObject _s: instanceManager.instanceAt(index)
        // What the combo shows: the live modelName, except right after a
        // failed load, where the old model is shown (mock state ③ rebound).
        readonly property string _display: {
            const s = irow._s
            if (!s)
                return ""
            if (irow._rolledBack && s.modelName === irow._failedName)
                return irow._prevModel
            return s.modelName
        }
        property string _prevModel: ""
        property string _failedName: ""
        property bool _rolledBack: false

        width: parent.width
        implicitHeight: Math.max(_labels.implicitHeight, 32) + 2 * 10

        Column {
            id: _labels
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: _slot.left
            anchors.rightMargin: 16
            spacing: 2

            Row {
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: irow.label
                    color: Theme.textColor
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                StatusPill {
                    anchors.verticalCenter: parent.verticalCenter
                    status: irow._s && irow._s.status === "running"
                            ? "running" : "stopped"
                    label: irow._s && irow._s.status === "running"
                           ? qsTr("运行中") : qsTr("已停止")
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: root._mutedColor
                font.pixelSize: 11
                text: {
                    const s = irow._s
                    if (!s)
                        return ""
                    const cur = qsTr("当前 %1").arg(irow._display)
                    // Offline switching is config-only (backend contract).
                    return s.status === "running"
                        ? cur + qsTr(" · 切换后即时生效")
                        : cur + qsTr(" · 切换将于下次启动时生效")
                }
            }
        }

        Item {
            id: _slot
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: childrenRect.height

            AppComboBox {
                id: combo
                width: 168
                model: root.allNames
                currentIndex: {
                    const s = irow._s
                    return s ? combo.find(irow._display) : -1
                }
                onActivated: (i) => {
                    const s = irow._s
                    if (!s || combo.currentText.length === 0)
                        return
                    // Record the rollback target before the switch.
                    irow._prevModel = s.modelName
                    irow._failedName = ""
                    irow._rolledBack = false
                    // Running: immediate load_model; stopped: config-only
                    // (backend contract) — same call either way. S7: write
                    // goes through the API bridge (instanceControl holds
                    // the shared IInstanceControlApi); the model_load_failed
                    // rollback feedback below keeps listening to the live
                    // session signal (reads stay on the live object).
                    instanceControl.loadModel(s.uuid, combo.currentText)
                }
            }
        }

        Connections {
            target: irow._s
            // modelLoadFailureRevision's NOTIFY signal is modelLoadFailed
            // (custom name — not the default <property>Changed).
            function onModelLoadFailed() {
                const s = irow._s
                if (!s || s.lastModelLoadError.length === 0)
                    return
                irow._failedName = s.modelName
                irow._rolledBack = true
                root.notifyModelLoadFailed(s.label, s.modelName,
                                           irow._prevModel,
                                           s.lastModelLoadError)
            }
        }
    }

    // ── Page ───────────────────────────────────────────────────────────
    Flickable {
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: contentCol.implicitHeight + 48
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: contentCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            spacing: Theme.spaceGroup
            topPadding: 28

            // ── Header ─────────────────────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("模型库")
                    color: Theme.textColor
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppButton {
                        style: "subtle"
                        text: qsTr("📂 打开模型目录")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: modelLibrary.openModelDir()
                    }
                    AppButton {
                        style: "primary"
                        text: qsTr("↻ 重新扫描")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: modelLibrary.rescan()
                    }
                }
            }
            Text {
                text: {
                    modelLibrary.revision
                    return qsTr("扫描自 %1 · 共发现 %2 个模型 · 选中模型可查看动作 / 表情 / 命中区域")
                        .arg(modelLibrary.modelsDir()).arg(modelLibrary.modelCount)
                }
                color: root._mutedColor
                font.pixelSize: 13
            }

            // ── Empty state ────────────────────────────────────────
            Card {
                width: parent.width
                visible: modelLibrary.modelCount === 0
                title: qsTr("未发现模型")

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._mutedColor
                    font.pixelSize: 13
                    text: {
                        modelLibrary.revision
                        return qsTr("将包含 <名称>.model3.json 的模型目录放入：\n%1")
                            .arg(modelLibrary.modelsDir())
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._faintColor
                    font.pixelSize: 11
                    text: qsTr("放入后点击右上角「重新扫描」即可发现。")
                }
            }

            // ── Two-column zone ────────────────────────────────────
            Row {
                width: parent.width
                spacing: Theme.spaceGroup
                visible: modelLibrary.modelCount > 0

                // LEFT 58%: library + detail
                Column {
                    width: (parent.width - Theme.spaceGroup) * 0.58
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("模型库")
                        hint: qsTr("ModelScanner · 目录含 <名称>.model3.json 即收录")

                        Column {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: modelLibrary.modelCount
                                delegate: Rectangle {
                                    id: modelRow
                                    required property int index
                                    readonly property string _name: {
                                        modelLibrary.revision
                                        return modelLibrary.modelDirName(index)
                                    }
                                    readonly property var _grad:
                                        root._gradFor(_name)
                                    readonly property int _use:
                                        root._usageCount(_name)
                                    readonly property bool sel:
                                        root.selectedModel === _name

                                    width: parent.width
                                    height: 64
                                    radius: Theme.radiusMd
                                    color: sel ? Theme.accentAlpha(0.10)
                                               : "transparent"
                                    border.width: sel ? 1 : 0
                                    border.color: Theme.accentColor

                                    Row {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 12

                                        Rectangle {
                                            width: 36; height: 36; radius: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            gradient: Gradient {
                                                GradientStop {
                                                    position: 0
                                                    color: modelRow._grad[0]
                                                }
                                                GradientStop {
                                                    position: 1
                                                    color: modelRow._grad[1]
                                                }
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                text: modelRow._name.charAt(0).toUpperCase()
                                                color: "#ffffff"
                                                font.pixelSize: 16
                                                font.weight: Font.DemiBold
                                            }
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2
                                            Text {
                                                text: modelRow._name
                                                color: Theme.textColor
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                text: {
                                                    modelLibrary.revision
                                                    return root._statLine(
                                                        modelLibrary.modelMotionGroupCount(modelRow.index),
                                                        modelLibrary.modelExpressionCount(modelRow.index),
                                                        modelLibrary.modelHitAreaCount(modelRow.index))
                                                }
                                                color: root._faintColor
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                    StatusPill {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        status: modelRow._use > 0
                                                ? "running" : "stopped"
                                        label: modelRow._use > 0
                                               ? qsTr("使用中 ×%1").arg(modelRow._use)
                                               : qsTr("未使用")
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.selectedModel =
                                            modelRow._name
                                    }
                                }
                            }
                        }
                    }

                    Card {
                        id: detailCard
                        width: parent.width
                        visible: root.selectedModel.length > 0
                        title: qsTr("%1 · 模型详情").arg(root.selectedModel)
                        hint: qsTr("数据解析自 %1.model3.json · 动作 Chip 点击即试播")
                            .arg(root.selectedModel)

                        readonly property var _groups: {
                            modelLibrary.revision
                            return root.selectedModel.length > 0
                                ? modelLibrary.modelMotionGroups(root.selectedModel)
                                : []
                        }
                        readonly property var _exprs: {
                            modelLibrary.revision
                            return root.selectedModel.length > 0
                                ? modelLibrary.modelExpressions(root.selectedModel)
                                : []
                        }
                        readonly property var _hits: {
                            modelLibrary.revision
                            return root.selectedModel.length > 0
                                ? modelLibrary.modelHitAreas(root.selectedModel)
                                : []
                        }

                        actionItem: AppButton {
                            style: "subtle"
                            text: qsTr("应用到全部实例")
                            implicitHeight: 26
                            enabled: instanceManager.rowCount() > 0
                            onClicked: {
                                const labels = []
                                for (let i = 0; i < instanceManager.rowCount(); ++i) {
                                    const s = instanceManager.instanceAt(i)
                                    if (s)
                                        labels.push(s.label)
                                }
                                applyAllDialog._targets = labels.join("、")
                                applyAllDialog.open()
                            }
                        }

                        // ── 动作组（可试播） ──────────────────────
                        Text {
                            width: parent.width
                            font.pixelSize: 11
                            color: root._faintColor
                            text: qsTr("动作组(%1)· %2")
                                .arg(detailCard._groups.length)
                                .arg(root._trialRowFor(root.selectedModel) >= 0
                                     ? qsTr("点击试播到首个运行中的目标实例")
                                     : qsTr("暂无运行中实例正在使用该模型，试播不可用"))
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: detailCard._groups
                                delegate: Chip {
                                    required property string modelData
                                    text: modelData
                                    // Trial-play needs a running instance on
                                    // THIS model; whole group dims together.
                                    enabled: root._trialRowFor(
                                        root.selectedModel) >= 0
                                    opacity: enabled ? 1.0 : 0.45
                                    onActivated: {
                                        const row = root._trialRowFor(
                                            root.selectedModel)
                                        if (row >= 0)
                                            instanceControl.playMotion(
                                                instanceManager.instanceAt(row).uuid,
                                                modelData, 0)
                                    }
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            visible: detailCard._groups.length === 0
                            text: qsTr("（该模型未定义动作组）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }

                        // ── 表情（只读） ───────────────────────────
                        Text {
                            width: parent.width
                            font.pixelSize: 11
                            color: root._faintColor
                            text: qsTr("表情(%1)· 只读")
                                .arg(detailCard._exprs.length)
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: detailCard._exprs
                                delegate: Chip {
                                    required property string modelData
                                    text: modelData
                                    enabled: false   // read-only display
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            visible: detailCard._exprs.length === 0
                            text: qsTr("（该模型未定义表情）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }

                        // ── 命中区域（只读） ───────────────────────
                        Text {
                            width: parent.width
                            font.pixelSize: 11
                            color: root._faintColor
                            text: qsTr("命中区域(%1)· 只读")
                                .arg(detailCard._hits.length)
                        }
                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: detailCard._hits
                                delegate: Chip {
                                    required property string modelData
                                    text: modelData
                                    enabled: false   // read-only display
                                }
                            }
                        }
                        Row {
                            width: parent.width
                            visible: detailCard._hits.length > 0
                            spacing: 8
                            AppButton {
                                style: "subtle"
                                text: qsTr("去实例页试触发 →")
                                implicitHeight: 26
                                onClicked: root.requestSwitchPage("instance")
                            }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            visible: detailCard._hits.length === 0
                            text: qsTr("（该模型未定义命中区域）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }
                    }
                }

                // RIGHT 42%: per-instance assignment + default model
                Column {
                    width: (parent.width - Theme.spaceGroup) * 0.42
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("应用到实例")
                        hint: qsTr("运行中即时下发 load_model · 未启动仅写配置，下次启动生效")

                        Column {
                            width: parent.width
                            spacing: 0

                            Repeater {
                                model: instanceManager
                                delegate: InstanceModelRow {}
                            }

                            Text {
                                visible: instanceManager.rowCount() === 0
                                text: qsTr("（暂无实例）")
                                color: root._mutedColor
                                font.pixelSize: 11
                                topPadding: 8
                                bottomPadding: 8
                            }
                        }
                    }

                    Card {
                        width: parent.width
                        title: qsTr("新实例默认模型")
                        hint: qsTr("保存于面板配置 · 创建实例对话框按此预选")

                        SettingRow {
                            width: parent.width
                            title: qsTr("默认模型")
                            desc: qsTr("新建实例时预选；创建对话框中仍可更改")

                            AppComboBox {
                                id: defaultModelCombo
                                width: 168
                                model: root.allNames
                                currentIndex: {
                                    const i = defaultModelCombo.find(
                                        panelConfig.defaultModelName)
                                    return i >= 0 ? i : 0
                                }
                                onActivated: (i) => {
                                    panelConfig.defaultModelName =
                                        defaultModelCombo.currentText
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Dialogs ────────────────────────────────────────────────────────

    // Mock state ③: load failure → bounce notice. AppDialog is plain-text,
    // so the renderer error line is folded into the message.
    AppDialog {
        id: failDialog
        title: qsTr("切换失败")
        showNegative: false
        positiveText: qsTr("知道了")
        message: ""
    }

    AppDialog {
        id: applyAllDialog
        title: qsTr("应用到全部实例")
        positiveText: qsTr("应用")
        property string _targets: ""
        message: qsTr("将把「%1」应用到以下实例：%2。\n运行中的实例即时切换，未启动的将在下次启动时生效。")
            .arg(root.selectedModel).arg(_targets)
        onPositiveClicked: {
            // S7: writes through the shared API bridge (int error return
            // ignored here — failures surface per-row via modelLoadFailed,
            // the same rollback path as the single switch).
            for (let i = 0; i < instanceManager.rowCount(); ++i) {
                const s = instanceManager.instanceAt(i)
                if (s)
                    instanceControl.loadModel(s.uuid, root.selectedModel)
            }
        }
    }
}
