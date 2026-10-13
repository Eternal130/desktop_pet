import QtQuick
import QtQuick.Controls
import QtQuick.Window
import DesktopPet

// Voice-pack page (design doc §4). Library + mapping preview left, mount
// matrix right (todo 21 wired: per-instance mount/unmount drives
// MountedBehaviorEngine via voicePacks.setInstanceVoicePack).
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color

    property int selectedPack: 0

    // Wave-2 selection memory: survives page switches via the window-level
    // key (Main.qml rememberedPackKey). The key is the pack PATH, not the
    // index — a rescan can reorder packs, and a vanished pack falls back
    // to index 0.
    Component.onCompleted: root._restoreSelection()

    function _restoreSelection() {
        const w = Window.window
        const key = w && w.rememberedPackKey !== undefined
                    ? String(w.rememberedPackKey) : ""
        if (key.length > 0) {
            for (let i = 0; i < voicePacks.packCount; ++i)
                if (voicePacks.packPath(i) === key) {
                    selectedPack = i
                    return
                }
        }
        selectedPack = voicePacks.packCount > 0 ? 0 : selectedPack
    }
    onSelectedPackChanged: {
        const w = Window.window
        if (w && w.rememberedPackKey !== undefined
                && selectedPack >= 0 && selectedPack < voicePacks.packCount)
            w.rememberedPackKey = voicePacks.packPath(selectedPack)
    }

    function _mountedRowForPack(packPath) {
        for (let i = 0; i < instanceManager.rowCount(); ++i) {
            const s = instanceManager.instanceAt(i)
            if (s && voicePacks.instanceVoicePack(s.uuid) === packPath)
                return i
        }
        return -1
    }

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
            topPadding: Theme.spacePage

            // ── Header ─────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("语音包")
                    color: Theme.textColor
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppButton {
                        style: "subtle"
                        iconName: "folderopen"
                        text: qsTr("打开语音包目录")
                        anchors.verticalCenter: parent.verticalCenter
                        // C++ side: mkpath + QDesktopServices with
                        // QUrl::fromLocalFile. String-concatenating
                        // "file:///" + absolute path yields four slashes on
                        // Linux → invalid URL → silent no-op.
                        onClicked: voicePacks.openVoicePackDir()
                    }
                    AppButton {
                        style: "primary"
                        iconName: "restart"
                        text: qsTr("重新扫描")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: voicePacks.rescan()
                    }
                }
            }
            Text {
                text: qsTr("通过语音包内的 meta.mko 清单识别" +
                           " · 挂载后行为引擎优先处理命中与待机")
                color: _mutedColor
                font.pixelSize: 13
            }

            // ── Empty state ────────────────────────────────────────────
            Card {
                width: parent.width
                visible: voicePacks.packCount === 0
                title: qsTr("未发现语音包")

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._mutedColor
                    font.pixelSize: 13
                    text: qsTr("将包含 meta.mko 的语音包目录放入：%1").arg(
                        voicePacks.voicePackDir())
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._faintColor
                    font.pixelSize: 11
                    text: qsTr("放入后点击右上角「重新扫描」即可发现。")
                }
            }

            // ── Two-column zone ────────────────────────────────────────
            Row {
                width: parent.width
                spacing: Theme.spaceGroup
                visible: voicePacks.packCount > 0

                // LEFT: library + mapping preview
                Column {
                    width: (parent.width - Theme.spaceGroup) * 0.58
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("语音包库")

                        Column {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: voicePacks.packCount
                                delegate: Rectangle {
                                    width: parent.width
                                    height: 64
                                    radius: Theme.radiusMd
                                    readonly property bool sel:
                                        root.selectedPack === index
                                    color: sel ? Qt.alpha(Theme.accentColor, 0.10)
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
                                                GradientStop { position: 0; color: "#ffd9a8" }
                                                GradientStop { position: 1; color: "#f0a860" }
                                            }
                                            // vector mic (was the 🎙 emoji —
                                            // tofu on emoji-font-less hosts);
                                            // white reads on the warm tile
                                            AppIcon {
                                                anchors.centerIn: parent
                                                name: "mic"
                                                size: 18
                                                color: "#ffffff"
                                            }
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2
                                            Text {
                                                text: voicePacks.packDisplayName(index)
                                                color: Theme.textColor
                                                font.pixelSize: 13
                                                font.weight: Font.DemiBold
                                            }
                                            Text {
                                                text: qsTr("%1 组行为映射 · %2 条语音 · %3")
                                                    .arg(voicePacks.packGroupCount(index))
                                                    .arg(voicePacks.packActionCount(index))
                                                    .arg(voicePacks.packDirName(index))
                                                color: root._faintColor
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.selectedPack = index
                                    }
                                }
                            }
                        }
                    }

                    Card {
                        width: parent.width
                        title: voicePacks.packCount > root.selectedPack
                            ? qsTr("%1 · 行为映射预览").arg(
                                  voicePacks.packDisplayName(root.selectedPack))
                            : qsTr("行为映射预览")
                        hint: qsTr("MountedBehaviorEngine · 挂载后优先于默认命中处理")

                        Flow {
                            width: parent.width
                            spacing: 8
                            Repeater {
                                model: voicePacks.packCount > root.selectedPack
                                    ? voicePacks.packGroupNames(root.selectedPack)
                                    : []
                                // Clicking a mapping-group chip trial-triggers
                                // that group on the first instance that has THIS
                                // pack mounted. The chip displays the group NAME
                                // but triggers by CODE — the behavior engine
                                // matches hit areas by map key (code), not the
                                // human-readable name.
                                delegate: Chip {
                                    text: voicePacks.packGroupNames(
                                        root.selectedPack)[index]
                                    opacity: root._mountedRowForPack(
                                        voicePacks.packPath(root.selectedPack)) >= 0
                                        ? 1.0 : 0.45
                                    onActivated: {
                                        const row = root._mountedRowForPack(
                                            voicePacks.packPath(root.selectedPack))
                                        if (row >= 0)
                                            instanceControl.triggerHitArea(
                                                instanceManager.instanceAt(row).uuid,
                                                voicePacks.packGroupCodes(
                                                    root.selectedPack)[index])
                                    }
                                }
                            }
                        }
                        Text {
                            visible: voicePacks.packCount > root.selectedPack
                                     && voicePacks.packGroupNames(
                                            root.selectedPack).length === 0
                            text: qsTr("（该语音包未定义行为映射组）")
                            color: root._mutedColor
                            font.pixelSize: 11
                        }
                    }
                }

                // RIGHT: mount matrix (todo 21 wired — live mount/unmount)
                Column {
                    width: (parent.width - Theme.spaceGroup) * 0.42
                    spacing: Theme.spaceGroup

                    Card {
                        width: parent.width
                        title: qsTr("挂载到实例")
                        hint: qsTr("挂载/卸载即时下发 · 卸载后恢复默认命中处理")

                        Column {
                            width: parent.width
                            spacing: 12

                            Repeater {
                                model: instanceManager
                                delegate: SettingRow {
                                    id: mountRow
                                    // Capture the row uuid OUTSIDE the combo
                                    // scope: ComboBox declares its own `model`
                                    // property, which would shadow the
                                    // Repeater row's `model.uuid` inside
                                    // AppComboBox bindings/handlers.
                                    readonly property string instUuid: model.uuid
                                    width: parent.width
                                    title: label
                                    desc: voicePacks.mountsRevision >= 0
                                        ? (qsTr("当前：") + (
                                            voicePacks.instanceVoicePack(mountRow.instUuid).length > 0
                                            ? voicePacks.packDisplayNameFor(
                                                  voicePacks.instanceVoicePack(mountRow.instUuid))
                                            : qsTr("无")))
                                        : ""
                                    // P1-5: was a per-row Segmented — overflowed
                                    // horizontally once packs outnumbered the
                                    // row width (right options clipped), and
                                    // matched selections by DISPLAY NAME, so
                                    // duplicate display names mounted the wrong
                                    // pack. Index-addressed combo instead:
                                    // index 0 = 无 (unmount), index i > 0 mounts
                                    // packPath(i - 1); the current selection is
                                    // recomputed by PATH equality
                                    // (instanceVoicePack vs packPath).
                                    // Display names are labels only, never keys.
                                    AppComboBox {
                                        width: 168
                                        model: {
                                            const opts = [qsTr("无")]
                                            for (let i = 0; i < voicePacks.packCount; ++i)
                                                opts.push(voicePacks.packDisplayName(i))
                                            return opts
                                        }
                                        currentIndex: {
                                            voicePacks.mountsRevision
                                            const mounted = voicePacks.instanceVoicePack(
                                                mountRow.instUuid)
                                            if (mounted.length === 0)
                                                return 0
                                            for (let j = 0; j < voicePacks.packCount; ++j)
                                                if (voicePacks.packPath(j) === mounted)
                                                    return j + 1
                                            return 0
                                        }
                                        onActivated: (i) => {
                                            voicePacks.setInstanceVoicePack(
                                                mountRow.instUuid,
                                                i === 0 ? "" : voicePacks.packPath(i - 1))
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: instanceManager.rowCount() === 0
                                text: qsTr("（暂无实例）")
                                color: root._mutedColor
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }
    }
}
