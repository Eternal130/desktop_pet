import QtQuick
import QtQuick.Controls
import DesktopPet

// 插件管理 page — the standalone, page-sized upgrade of SettingsPage's
// pluginsCard (P4 §B.6 management UI + v1.12 drop-in stage). The settings
// card stays untouched; this page is the full-surface version.
//
// One card per registry entry: status icon + title/version + status pill +
// source badge (内置/已安装) + capability chips + enable toggle. Dynamic
// plugins additionally show their library path (truncated, full path on
// hover tooltip). Error rows surface errorMessage in a tinted block WITH
// the internal pluginId — the id is the troubleshooting handle, so it is
// hidden on healthy rows by design.
//
// Data surface: the `pluginManager` context property ONLY
// (QAbstractListModel roles pluginId/title/version/status/errorMessage/
// enabled/capabilities/source/path + setEnabled()/restartPending()/
// openPluginsDir() + count). Enable/disable is a persisted config bit
// applied at next panel boot (hot unload forbidden, §A.4):
//   - the per-row "重启后生效" badge marks WHICH row is pending,
//   - the page-top warning banner marks THAT something is pending —
//     it aggregates PluginManager::restartPending(id) across all rows,
//     rescanned whenever any row's flag flips (restartPending is a plain
//     Q_INVOKABLE without NOTIFY, hence the explicit rescan).
Item {
    id: root

    readonly property color _mutedColor: Theme.text2Color
    readonly property color _faintColor: Theme.text3Color

    // ── Page-level restart-pending banner state ──────────────────────────
    // Scan the Repeater's live delegates for _pending rows. The list is a
    // handful of entries — a linear rescan on each flip is trivially cheap.
    property int _pendingCount: 0
    function _rescanPending() {
        let n = 0
        for (let i = 0; i < pluginRepeater.count; ++i) {
            const it = pluginRepeater.itemAt(i)
            if (it && it._pending) ++n
        }
        root._pendingCount = n
    }
    Component.onCompleted: root._rescanPending()

    // ── Status mapping (registry status string → StatusPill) ─────────────
    // started → green · failed → red · the rest (stopped = user-disabled,
    // registered/discovered/validated = never started) → the gray pill;
    // the label text carries the distinction the single color can't.
    function _pillStatus(s) {
        if (s === "started") return "running"
        if (s === "failed")  return "error"
        return "stopped"
    }
    function _pillLabel(s) {
        if (s === "started") return qsTr("运行中")
        if (s === "failed")  return qsTr("失败")
        if (s === "stopped") return qsTr("已停用")
        return qsTr("未启动")
    }
    // Leading icon tint: accent = running, error = failed, else muted.
    function _iconColor(s) {
        if (s === "started") return Theme.accentColor
        if (s === "failed")  return Theme.errorColor
        return Theme.text3Color
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

            // ── Header (anchored Item — title left, action right) ──────
            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("插件管理")
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
                        text: qsTr("打开插件目录")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: pluginManager.openPluginsDir()
                    }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("把编译好的插件文件（.so / .dll）放入插件目录，重启面板后自动加载。启用 / 停用为配置项，重启面板后生效。")
                color: root._mutedColor
                font.pixelSize: 13
            }

            // ── Restart-pending banner (InfoBar recipe, warning flavor) ─
            Rectangle {
                width: parent.width
                height: 34
                visible: root._pendingCount > 0
                radius: Theme.radiusMd
                color: Theme.warningBgColor
                border.color: Qt.alpha(Theme.warningColor, 0.3)
                border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: qsTr("⚠ 有插件的启停将在重启面板后生效")
                    color: Theme.warningColor
                    font.pixelSize: 12
                }
            }

            // ── Empty state (zero registry entries) ─────────────────────
            Card {
                width: parent.width
                visible: pluginManager.count === 0

                Column {
                    width: parent.width
                    spacing: 10

                    // icon disc (centered via a plain-Item wrapper — no
                    // horizontal anchors inside the positioner)
                    Item {
                        width: parent.width
                        height: 56
                        Rectangle {
                            anchors.centerIn: parent
                            width: 56; height: 56; radius: 28
                            color: Theme.offBgColor
                            AppIcon {
                                anchors.centerIn: parent
                                name: "puzzle"
                                size: 26
                                color: root._faintColor
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("未发现插件")
                        color: Theme.textColor
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("把编译好的插件文件（.so / .dll）放入插件目录，重启面板后自动加载。")
                        color: root._mutedColor
                        font.pixelSize: 12
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("内置插件随面板提供，无需安装；目录位置可点击下方按钮打开。")
                        color: root._faintColor
                        font.pixelSize: 11
                    }
                    Item {
                        width: parent.width
                        height: 32
                        AppButton {
                            anchors.centerIn: parent
                            style: "subtle"
                            iconName: "folderopen"
                            text: qsTr("打开插件目录")
                            onClicked: pluginManager.openPluginsDir()
                        }
                    }
                }
            }

            // ── Plugin cards (visible rows only — empty state above takes
            //    over at count 0) ────────────────────────────────────────
            Repeater {
                id: pluginRepeater
                model: pluginManager

                delegate: Rectangle {
                    id: pluginRow
                    required property string pluginId
                    required property string title
                    required property string version
                    required property string status
                    required property string errorMessage
                    required property bool enabled
                    required property var capabilities
                    required property string source
                    required property string path

                    // Pending flag with an explicit dependency on the row's
                    // `enabled` role: restartPending() is a Q_INVOKABLE with
                    // no NOTIFY, so referencing `enabled` makes the whole
                    // expression re-evaluate when dataChanged(EnabledRole)
                    // lands after setEnabled — same refresh-counter trick
                    // ModelLibraryPage plays on modelLibrary.revision.
                    readonly property bool _pending: {
                        pluginRow.enabled
                        return pluginManager.restartPending(pluginRow.pluginId)
                    }
                    on_PendingChanged: root._rescanPending()

                    width: parent.width
                    // Card recipe (radiusLg surface + hairline) — same visual
                    // as the Card component, custom interior for the row.
                    height: Math.max(_bodyCol.height + 24, 68)
                    radius: Theme.radiusLg
                    color: Theme.surfaceColor
                    border.width: 1
                    border.color: Theme.hairlineColor

                    // Leading status icon block (puzzle mark, tinted by
                    // runtime status — the at-a-glance column)
                    Rectangle {
                        x: 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36; height: 36; radius: 8
                        color: Theme.offBgColor
                        AppIcon {
                            anchors.centerIn: parent
                            name: "puzzle"
                            size: 18
                            color: root._iconColor(pluginRow.status)
                        }
                    }

                    // Content column — bounded left by the icon block and
                    // right by the toggle slot (both fixed widths)
                    Column {
                        id: _bodyCol
                        anchors.left: parent.left
                        anchors.leftMargin: 64
                        anchors.right: parent.right
                        anchors.rightMargin: 72
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        // Title row: title + version + status pill + source
                        // badge + per-row pending badge (vertical anchors
                        // inside Row are the sanctioned axis — x is the
                        // positioner-managed one)
                        Row {
                            width: parent.width
                            spacing: 8
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: pluginRow.title
                                color: Theme.textColor
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: pluginRow.version.length > 0
                                text: "v" + pluginRow.version
                                color: root._faintColor
                                font.pixelSize: 11
                            }
                            StatusPill {
                                anchors.verticalCenter: parent.verticalCenter
                                status: root._pillStatus(pluginRow.status)
                                label: root._pillLabel(pluginRow.status)
                            }
                            // Source badge: 内置 (neutral) vs 已安装
                            // (accent-tinted — the drop-in ones stand out)
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: _srcText.implicitWidth + 12
                                height: 18; radius: 9
                                color: pluginRow.source === "dynamic"
                                       ? Qt.alpha(Theme.accentColor, 0.14)
                                       : Theme.offBgColor
                                Text {
                                    id: _srcText
                                    anchors.centerIn: parent
                                    text: pluginRow.source === "dynamic"
                                          ? qsTr("已安装") : qsTr("内置")
                                    color: pluginRow.source === "dynamic"
                                           ? Theme.accentColor
                                           : root._mutedColor
                                    font.pixelSize: 10
                                }
                            }
                            // Per-row pending badge — WHICH row disagrees
                            // with its runtime state (page banner says THAT
                            // one is pending). Same recipe as the
                            // settings card's badge.
                            Rectangle {
                                visible: pluginRow._pending
                                anchors.verticalCenter: parent.verticalCenter
                                width: _pendingText.implicitWidth + 14
                                height: 18; radius: 9
                                color: Qt.alpha(Theme.accentColor, 0.14)
                                Text {
                                    id: _pendingText
                                    anchors.centerIn: parent
                                    text: qsTr("重启后生效")
                                    color: Theme.accentColor
                                    font.pixelSize: 10
                                }
                            }
                        }

                        // Capability chips — informational only (read-only
                        // ghost pills, hidden when the list is empty)
                        Flow {
                            width: parent.width
                            spacing: 6
                            visible: pluginRow.capabilities.length > 0
                            Repeater {
                                model: pluginRow.capabilities
                                delegate: Chip {
                                    required property string modelData
                                    text: modelData
                                    enabled: false
                                }
                            }
                        }

                        // Library path — dynamic plugins only. Middle-elided
                        // against the column width; full path on hover
                        // tooltip (only when actually truncated).
                        Item {
                            width: parent.width
                            height: 16
                            visible: pluginRow.source === "dynamic"
                                     && pluginRow.path.length > 0
                            AppIcon {
                                x: 0
                                anchors.verticalCenter: parent.verticalCenter
                                name: "folderopen"
                                size: 12
                                color: root._faintColor
                            }
                            Text {
                                id: _pathText
                                x: 18
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: pluginRow.path
                                color: root._faintColor
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                ToolTip.visible: _pathMa.containsMouse
                                                 && _pathText.truncated
                                ToolTip.text: pluginRow.path
                                ToolTip.delay: 350
                                MouseArea {
                                    id: _pathMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                }
                            }
                        }

                        // Error block — the ONLY place the internal
                        // pluginId shows (troubleshooting handle), beside
                        // the registry's error message.
                        Rectangle {
                            width: parent.width
                            height: _errCol.implicitHeight + 16
                            visible: pluginRow.errorMessage.length > 0
                            radius: Theme.radiusMd
                            color: Theme.errorBgColor
                            border.width: 1
                            border.color: Qt.alpha(Theme.errorColor, 0.3)

                            Column {
                                id: _errCol
                                x: 10; y: 8
                                width: parent.width - 20
                                spacing: 3
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    visible: pluginRow.errorMessage.length > 0
                                    text: qsTr("错误：") + pluginRow.errorMessage
                                    color: Theme.errorColor
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                }
                                Text {
                                    width: parent.width
                                    visible: pluginRow.errorMessage.length > 0
                                    text: qsTr("插件 ID：%1").arg(pluginRow.pluginId)
                                    color: Qt.alpha(Theme.errorColor, 0.75)
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    // Enable toggle slot — caption + switch in a plain-Item
                    // wrapper (horizontal anchors are illegal inside a
                    // positioner; they are fine inside a plain Item)
                    Item {
                        width: 40
                        height: 38
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            anchors.top: parent.top
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("启用")
                            color: root._faintColor
                            font.pixelSize: 10
                        }
                        ToggleSwitch {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            checked: pluginRow.enabled
                            onToggled: {
                                pluginManager.setEnabled(pluginRow.pluginId,
                                                         checked)
                                // TapHandler flipped `checked` imperatively,
                                // which broke the model binding — re-arm it
                                // so the switch keeps following
                                // PluginManager (dataChanged → EnabledRole
                                // → row.enabled).
                                checked = Qt.binding(function() {
                                    return pluginRow.enabled
                                })
                            }
                        }
                    }
                }
            }
        }
    }
}
