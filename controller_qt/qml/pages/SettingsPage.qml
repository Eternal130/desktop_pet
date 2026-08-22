import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import DesktopPet

// Settings page — Fluent UI redesign (design doc §⑤).
//
// Win11-settings layout: left anchor nav (行为/启动/外观/关于), right column
// of SettingRow-styled cards. Behavior data model unchanged: panelConfig +
// autoLaunch context properties drive the same four fields, persistence
// unchanged (QSaveFile atomic writes in PanelConfigController).
//
// New display-only sections: appearance (theme mode + accent swatches +
// window material) and about (version, protocol coverage, config dir).
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.mutedTextColor

    // Anchor sections (scroll positions resolved after layout).
    function _scrollTo(section) {
        const map = { "behavior": behaviorCard, "startup": startupCard,
                      "appearance": appearanceCard, "about": aboutCard }
        const item = map[section]
        if (item && flick.contentHeight > flick.height)
            flick.contentY = item.mapToItem(contentCol, 0, 0).y - 16
        activeSection = section
    }
    property string activeSection: "behavior"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacePage
        anchors.rightMargin: Theme.spacePage
        anchors.topMargin: 28
        spacing: 24

        // ── Left anchor nav ────────────────────────────────────────────────
        Column {
            Layout.preferredWidth: 140
            Layout.alignment: Qt.AlignTop
            spacing: 2

            FluText {
                text: qsTr("设置")
                font: FluTextStyle.Title
                bottomPadding: 16
            }
            FluToggleButton {
                width: 130
                text: qsTr("🔀 行为")
                checked: root.activeSection === "behavior"
                onClicked: root._scrollTo("behavior")
            }
            FluToggleButton {
                width: 130
                text: qsTr("🚀 启动")
                checked: root.activeSection === "startup"
                onClicked: root._scrollTo("startup")
            }
            FluToggleButton {
                width: 130
                text: qsTr("🎨 外观")
                checked: root.activeSection === "appearance"
                onClicked: root._scrollTo("appearance")
            }
            FluToggleButton {
                width: 130
                text: qsTr("ℹ 关于")
                checked: root.activeSection === "about"
                onClicked: root._scrollTo("about")
            }
        }

        // ── Right settings column ──────────────────────────────────────────
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: contentCol.implicitHeight + 48
            ScrollBar.vertical: FluScrollBar {}

            Column {
                id: contentCol
                width: flick.width
                spacing: Theme.spaceGroup

                // ── Behavior ───────────────────────────────────────────────
                SectionCard {
                    id: behaviorCard
                    width: parent.width
                    title: qsTr("行为")
                    hint: qsTr("关闭按钮与退出策略 · 立即持久化")

                    FluText { text: qsTr("关闭按钮行为"); font: FluTextStyle.BodyStrong }
                    Row {
                        spacing: 8
                        FluToggleButton {
                            text: qsTr("退出时关闭")
                            checked: panelConfig.closeAction === "exit"
                            onClicked: panelConfig.closeAction = "exit"
                        }
                        FluToggleButton {
                            text: qsTr("最小化到托盘")
                            checked: panelConfig.closeAction === "minimize"
                            onClicked: panelConfig.closeAction = "minimize"
                        }
                    }
                    SettingRow {
                        width: parent.width
                        title: qsTr("退出前确认")
                        desc: qsTr("开启后，点击关闭按钮时会弹出确认对话框。")
                        FluToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.confirmOnExit
                            onCheckedChanged: panelConfig.confirmOnExit = checked
                        }
                    }
                }

                // ── Startup ────────────────────────────────────────────────
                SectionCard {
                    id: startupCard
                    width: parent.width
                    title: qsTr("启动")

                    SettingRow {
                        width: parent.width
                        title: qsTr("开机自启动")
                        desc: qsTr("Windows 注册表 / Linux .desktop")
                        FluToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            Component.onCompleted: checked = autoLaunch.isEnabled()
                            onCheckedChanged: {
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
                        title: qsTr("启动时最小化")
                        desc: qsTr("开启后，程序启动时窗口将隐藏到系统托盘。")
                        FluToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: panelConfig.startMinimized
                            onCheckedChanged: panelConfig.startMinimized = checked
                        }
                    }
                }

                // ── Appearance ─────────────────────────────────────────────
                SectionCard {
                    id: appearanceCard
                    width: parent.width
                    title: qsTr("外观")

                    FluText { text: qsTr("主题模式"); font: FluTextStyle.BodyStrong }
                    Row {
                        spacing: 8
                        FluToggleButton {
                            text: qsTr("浅色")
                            checked: !FluTheme.dark
                            onClicked: FluTheme.dark = false
                        }
                        FluToggleButton {
                            text: qsTr("深色")
                            checked: FluTheme.dark
                            onClicked: FluTheme.dark = true
                        }
                    }

                    FluText { text: qsTr("强调色"); font: FluTextStyle.BodyStrong }
                    Row {
                        spacing: 8
                        Repeater {
                            model: [
                                { name: "Fluent Blue", c: "#005fb8" },
                                { name: "Indigo",      c: "#5b5bd6" },
                                { name: "Green",       c: "#0e700e" },
                                { name: "Amber",       c: "#9d5d00" },
                                { name: "Magenta",     c: "#c239b3" }
                            ]
                            delegate: Rectangle {
                                width: 26; height: 26
                                radius: Theme.radiusSm
                                color: modelData.c
                                border.width: Theme.accentColor.toString().toUpperCase()
                                              === modelData.c.toUpperCase() ? 3 : 1
                                border.color: border.width === 3
                                    ? Theme.textColor : Theme.borderColor
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: FluTheme.primaryColor = modelData.c
                                }
                            }
                        }
                    }
                    FluText {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("强调色同步驱动导航、按钮与监控图表调色板。")
                        color: root._mutedColor
                        font: FluTextStyle.Caption
                    }

                    SettingRow {
                        width: parent.width
                        title: qsTr("窗口材质")
                        desc: qsTr("亚克力（dwm-blur）· Win11 Mica / 旧系统自动降级")
                        FluText {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("亚克力")
                            color: root._mutedColor
                            font: FluTextStyle.Caption
                        }
                    }
                }

                // ── About ──────────────────────────────────────────────────
                SectionCard {
                    id: aboutCard
                    width: parent.width
                    title: qsTr("关于")

                    SettingRow {
                        width: parent.width
                        title: qsTr("Desktop Pet Controller (Qt)")
                        desc: qsTr("Qt %1 · MinGW 13.1 · FluentUI").arg(
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
                    }
                }
            }
        }
    }
}
