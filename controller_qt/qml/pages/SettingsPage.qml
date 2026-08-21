import QtQuick
import QtQuick.Controls
import FluentUI
import DesktopPet

// Settings page — FluentUI rewrite (feat/qt-fluentui-rewrite branch).
//
// Same data model as the classic page: panelConfig + autoLaunch context
// properties drive the four behavior controls; persistence is unchanged.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _mutedColor: Theme.mutedTextColor

    Flickable {
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: settingsColumn.implicitHeight + 48
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: settingsColumn
            width: root.width - 64
            x: 32
            spacing: 24
            topPadding: 32

            FluText {
                text: qsTr("Settings")
                font: FluTextStyle.TitleLarge
            }

            // ── Exit Behavior ───────────────────────────────────────────
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 16

                    FluText {
                        text: qsTr("退出行为")
                        font: FluTextStyle.Title
                        color: Theme.accentColor
                    }

                    FluText {
                        text: qsTr("关闭按钮行为")
                        font.pixelSize: 14
                    }

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

                    Row {
                        spacing: 12
                        FluToggleSwitch {
                            id: confirmExitSw
                            checked: panelConfig.confirmOnExit
                            onCheckedChanged: panelConfig.confirmOnExit = checked
                        }
                        FluText {
                            text: qsTr("退出前确认")
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    FluText {
                        text: qsTr("开启后，点击关闭按钮时会弹出确认对话框。")
                        color: root._mutedColor
                        font.pixelSize: 12
                        leftPadding: 58
                    }
                }
            }

            // ── Startup ─────────────────────────────────────────────────
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    width: parent.width
                    spacing: 16

                    FluText {
                        text: qsTr("启动")
                        font: FluTextStyle.Title
                        color: Theme.accentColor
                    }

                    Row {
                        spacing: 12
                        FluToggleSwitch {
                            id: autoLaunchSw
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
                        FluText {
                            text: qsTr("开机自启动")
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Row {
                        spacing: 12
                        FluToggleSwitch {
                            checked: panelConfig.startMinimized
                            onCheckedChanged: panelConfig.startMinimized = checked
                        }
                        FluText {
                            text: qsTr("启动时最小化")
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    FluText {
                        text: qsTr("开启后，程序启动时窗口将隐藏到系统托盘。")
                        color: root._mutedColor
                        font.pixelSize: 12
                        leftPadding: 58
                    }
                }
            }
        }
    }
}
