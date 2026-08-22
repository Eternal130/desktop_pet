import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import DesktopPet

// Voice-pack page — Fluent UI redesign Phase 5 (design doc §④).
//
// Two columns:
//   LEFT:  pack library (VoicePackScanner discovery + MetaMkoParser
//          metadata) + behavior-mapping preview chips for the selected pack.
//   RIGHT: per-instance mount matrix. Mount/unmount itself is pending core
//          wiring (todo 21 — populating MountedBehaviorEngine per instance),
//          so the matrix is rendered display-only with an explanatory note.
//
// Data source: `voicePacks` context property (VoicePackController).
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.mutedTextColor
    readonly property color _faintColor: Qt.rgba(
        Theme.textColor.r, Theme.textColor.g, Theme.textColor.b, 0.35)

    property int selectedPack: 0

    Flickable {
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: contentCol.implicitHeight + 48
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: contentCol
            width: root.width - 2 * Theme.spacePage
            x: Theme.spacePage
            spacing: Theme.spaceGroup
            topPadding: 28

            // ── Header ─────────────────────────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 12
                FluText {
                    text: qsTr("语音包")
                    font: FluTextStyle.Title
                    Layout.fillWidth: true
                }
                FluFilledButton {
                    text: qsTr("↻ 重新扫描")
                    onClicked: voicePacks.rescan()
                }
            }
            FluText {
                text: qsTr("发现于 meta.mko · 手写 protobuf 解析（无 libprotobuf 依赖）")
                color: _mutedColor
                font: FluTextStyle.Caption
            }

            // ── Empty state ────────────────────────────────────────────────
            SectionCard {
                width: parent.width
                visible: voicePacks.packCount === 0
                title: qsTr("未发现语音包")

                FluText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._mutedColor
                    font: FluTextStyle.Body
                    text: qsTr("将包含 meta.mko 的语音包目录放入：%1").arg(
                        voicePacks.voicePackDir())
                }
                FluText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: root._faintColor
                    font: FluTextStyle.Caption
                    text: qsTr("放入后点击右上角「重新扫描」即可发现。")
                }
            }

            // ── Two-column zone ────────────────────────────────────────────
            RowLayout {
                width: parent.width
                spacing: Theme.spaceGroup
                visible: voicePacks.packCount > 0

                // LEFT: library + mapping preview
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1.4
                    spacing: Theme.spaceGroup

                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("语音包库")

                        Column {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: voicePacks.packCount
                                delegate: Rectangle {
                                    width: parent.width
                                    height: 56
                                    radius: Theme.radiusMd
                                    readonly property bool sel:
                                        root.selectedPack === index
                                    color: sel
                                        ? Qt.rgba(Theme.accentColor.r,
                                                  Theme.accentColor.g,
                                                  Theme.accentColor.b, 0.10)
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
                                            color: Qt.rgba(Theme.accentColor.r,
                                                           Theme.accentColor.g,
                                                           Theme.accentColor.b, 0.16)
                                            Text {
                                                anchors.centerIn: parent
                                                text: "🎙"; font.pixelSize: 16
                                            }
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2
                                            FluText {
                                                text: voicePacks.packDisplayName(index)
                                                font: FluTextStyle.BodyStrong
                                            }
                                            FluText {
                                                text: qsTr("%1 组行为映射 · %2 条语音 · %3")
                                                    .arg(voicePacks.packGroupCount(index))
                                                    .arg(voicePacks.packActionCount(index))
                                                    .arg(voicePacks.packDirName(index))
                                                color: root._faintColor
                                                font: FluTextStyle.Caption
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.selectedPack = index
                                    }
                                }
                            }
                        }
                    }

                    SectionCard {
                        Layout.fillWidth: true
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
                                delegate: Chip { text: modelData }
                            }
                        }
                        FluText {
                            visible: voicePacks.packCount > root.selectedPack
                                     && voicePacks.packGroupNames(
                                            root.selectedPack).length === 0
                            text: qsTr("(该语音包未定义行为映射组)")
                            color: root._mutedColor
                            font: FluTextStyle.Caption
                        }
                    }
                }

                // RIGHT: mount matrix
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: Theme.spaceGroup

                    SectionCard {
                        Layout.fillWidth: true
                        title: qsTr("挂载到实例")

                        FluText {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            color: Theme.warningColor
                            font: FluTextStyle.Caption
                            text: qsTr("⚠ 挂载接线尚未完成（核心侧 todo 21）：当前版本" +
                                       "可浏览语音包元数据，挂载/卸载操作将在核心接线" +
                                       "后启用。")
                        }

                        Column {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: instanceManager
                                delegate: Row {
                                    width: parent.width
                                    spacing: 12

                                    FluText {
                                        text: label
                                        font: FluTextStyle.BodyStrong
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                    FluText {
                                        text: qsTr("未挂载")
                                        color: root._faintColor
                                        font: FluTextStyle.Caption
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }

                            FluText {
                                visible: instanceManager.rowCount() === 0
                                text: qsTr("(暂无实例)")
                                color: root._mutedColor
                                font: FluTextStyle.Caption
                            }
                        }
                    }
                }
            }
        }
    }
}
