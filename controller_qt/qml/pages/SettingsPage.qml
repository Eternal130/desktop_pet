import QtQuick
import QtQuick.Controls
import DesktopPet

// Settings page (design doc §5). Anchor nav left + SettingRow cards right.
// Behavior fields unchanged (panelConfig / autoLaunch). Three-way
// closeAction (exit / minimize / ask) + accent swatches + theme mode.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.text2Color

    function _scrollTo(section) {
        const map = { "behavior": behaviorCard, "startup": startupCard,
                      "appearance": appearanceCard, "about": aboutCard }
        const item = map[section]
        if (item && flick.contentHeight > flick.height)
            flick.contentY = item.mapToItem(contentCol, 0, 0).y - 16
        activeSection = section
    }
    property string activeSection: "behavior"

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacePage
        anchors.rightMargin: Theme.spacePage
        anchors.topMargin: 28
        spacing: 24

        // ── Left anchor nav ────────────────────────────────────────────
        Column {
            width: 150
            spacing: 2

            Text {
                text: qsTr("设置")
                color: Theme.textColor
                font.pixelSize: 22
                font.weight: Font.DemiBold
                bottomPadding: 16
            }
            AnchorNavItem { text: qsTr("🔀 行为"); section: "behavior" }
            AnchorNavItem { text: qsTr("🚀 启动"); section: "startup" }
            AnchorNavItem { text: qsTr("🎨 外观"); section: "appearance" }
            AnchorNavItem { text: qsTr("ℹ 关于"); section: "about" }
        }

        // ── Right settings column ──────────────────────────────────────
        Flickable {
            id: flick
            width: root.width - 2 * Theme.spacePage - 150 - 24
            height: parent.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: contentCol.implicitHeight + 48
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: contentCol
                width: flick.width
                spacing: Theme.spaceGroup

                Card {
                    id: behaviorCard
                    width: parent.width
                    title: qsTr("关闭按钮行为")

                    Segmented {
                        options: [ { label: qsTr("退出程序"), value: "exit" },
                                   { label: qsTr("最小化到托盘"), value: "minimize" },
                                   { label: qsTr("每次询问"), value: "ask" } ]
                        currentValue: panelConfig.closeAction
                        onSelected: (v) => { panelConfig.closeAction = v }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("选择「每次询问」时将弹出确认对话框。")
                        color: root._mutedColor
                        font.pixelSize: 11
                    }
                }

                Card {
                    width: parent.width
                    title: qsTr("退出")

                    SettingRow {
                        width: parent.width
                        title: qsTr("退出前确认")
                        desc: qsTr("点击关闭按钮时弹出确认对话框")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.confirmOnExit
                            onToggled: panelConfig.confirmOnExit = checked
                        }
                    }
                }

                Card {
                    id: startupCard
                    width: parent.width
                    title: qsTr("启动")

                    SettingRow {
                        width: parent.width
                        title: qsTr("开机自启动")
                        desc: qsTr("Windows 注册表 / Linux .desktop")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            property bool _syncing: true
                            Component.onCompleted: {
                                checked = autoLaunch.isEnabled()
                                _syncing = false
                            }
                            onToggled: {
                                if (_syncing) return
                                if (checked) {
                                    autoLaunch.enable()
                                    panelConfig.autoLaunchSystem = true
                                } else {
                                    autoLaunch.disable()
                                    panelConfig.autoLaunchSystem = false
                                }
                            }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("启动时最小化到托盘")
                        desc: qsTr("程序启动时窗口直接隐藏")
                        ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.startMinimized
                            onToggled: panelConfig.startMinimized = checked
                        }
                    }
                }

                Card {
                    id: appearanceCard
                    width: parent.width
                    title: qsTr("外观")

                    SettingRow {
                        width: parent.width
                        title: qsTr("主题模式")
                        desc: qsTr("跟随系统或手动指定")
                        Segmented {
                            options: [qsTr("浅色"), qsTr("深色")]
                            currentValue: Theme.dark ? qsTr("深色") : qsTr("浅色")
                            onSelected: (v) => { Theme.setDarkMode(v === qsTr("深色")) }
                        }
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("强调色")
                        desc: qsTr("导航/按钮/图表统一色相")
                        Row {
                            spacing: 8
                            Repeater {
                                model: [
                                    "#5b5bd6", "#0078d4", "#107c10",
                                    "#ca5010", "#c239b3"
                                ]
                                delegate: Rectangle {
                                    required property string modelData
                                    width: 22; height: 22
                                    radius: Theme.radiusSm
                                    color: modelData
                                    border.width: Theme.accentColor.toString()
                                                  .toUpperCase() === modelData.toUpperCase()
                                                  ? 2 : 0
                                    border.color: Theme.textColor
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: Theme.accentColor = parent.modelData
                                    }
                                }
                            }
                        }
                    }
                }

                Card {
                    id: aboutCard
                    width: parent.width
                    title: qsTr("关于")

                    SettingRow {
                        width: parent.width
                        title: qsTr("Desktop Pet Controller (Qt)")
                        desc: qsTr("v2.0 · Qt %1 · 原生 QML Fluent 风格").arg(
                            envChecker.qtVersion)
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("协议覆盖")
                        desc: qsTr("25 命令 · 14 事件 · WS 127.0.0.1:9001")
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("日志与配置")
                        desc: qsTr("~/.config/desktop-pet/ · spdlog 轮转日志")
                        AppButton {
                            style: "subtle"
                            text: qsTr("打开目录")
                            implicitHeight: 26; fontSize: 12
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: Qt.openUrlExternally(
                                "file:///" + envChecker.configDirectory)
                        }
                    }
                }
            }
        }
    }

    component AnchorNavItem : Rectangle {
        id: navBtn
        property string text: ""
        property string section: ""
        width: 130
        height: 34
        radius: Theme.radiusMd
        readonly property bool sel: root.activeSection === section
        color: sel ? Theme.accentAlpha(0.12)
            : (_hover.hovered ? Theme.withAlpha(Theme.textColor, 0.05) : "transparent")

        Rectangle {
            visible: navBtn.sel
            x: -8; y: parent.height * 0.25
            width: 3; height: parent.height * 0.5
            radius: 2
            color: Theme.accentColor
        }
        Text {
            anchors.centerIn: parent
            text: navBtn.text
            color: navBtn.sel ? Theme.accentColor : Theme.textColor
            font.pixelSize: 13
            font.weight: navBtn.sel ? Font.DemiBold : Font.Normal
        }
        HoverHandler { id: _hover }
        TapHandler { onTapped: root._scrollTo(navBtn.section) }
    }
}
