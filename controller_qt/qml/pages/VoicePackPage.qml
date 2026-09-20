import QtQuick
import QtQuick.Controls
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
            topPadding: 28

            // ── Header ─────────────────────────────────────────────────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("语音包")
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
                        text: qsTr("📂 打开语音包目录")
                        anchors.verticalCenter: parent.verticalCenter
                        // C++ side: mkpath + QDesktopServices with
                        // QUrl::fromLocalFile. String-concatenating
                        // "file:///" + absolute path yields four slashes on
                        // Linux → invalid URL → silent no-op.
                        onClicked: voicePacks.openVoicePackDir()
                    }
                    AppButton {
                        style: "primary"
                        text: qsTr("↻ 重新扫描")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: voicePacks.rescan()
                    }
                }
            }
            Text {
                text: qsTr("发现于 meta.mko · 手写 protobuf 解析（无 libprotobuf 依赖）" +
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
                        hint: qsTr("VoicePackScanner")

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
                                                GradientStop { position: 0; color: "#ffd9a8" }
                                                GradientStop { position: 1; color: "#f0a860" }
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                text: "🎙"; font.pixelSize: 16
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
                                            instanceManager.instanceAt(row)
                                                .triggerHitArea(
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
                                    width: parent.width
                                    title: label
                                    desc: voicePacks.mountsRevision >= 0
                                        ? (qsTr("当前：") + (
                                            voicePacks.instanceVoicePack(model.uuid).length > 0
                                            ? voicePacks.packDisplayNameFor(
                                                  voicePacks.instanceVoicePack(model.uuid))
                                            : qsTr("无")))
                                        : ""
                                    Segmented {
                                        options: {
                                            const opts = [qsTr("无")]
                                            for (let i = 0; i < voicePacks.packCount; ++i)
                                                opts.push(voicePacks.packDisplayName(i))
                                            return opts
                                        }
                                        currentValue: {
                                            voicePacks.mountsRevision
                                            const mounted = voicePacks.instanceVoicePack(model.uuid)
                                            if (mounted.length === 0) return qsTr("无")
                                            for (let i = 0; i < voicePacks.packCount; ++i)
                                                if (voicePacks.packPath(i) === mounted)
                                                    return voicePacks.packDisplayName(i)
                                            return qsTr("无")
                                        }
                                        onSelected: (v) => {
                                            if (v === qsTr("无")) {
                                                voicePacks.setInstanceVoicePack(model.uuid, "")
                                                return
                                            }
                                            for (let i = 0; i < voicePacks.packCount; ++i)
                                                if (voicePacks.packDisplayName(i) === v)
                                                    voicePacks.setInstanceVoicePack(
                                                        model.uuid, voicePacks.packPath(i))
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
