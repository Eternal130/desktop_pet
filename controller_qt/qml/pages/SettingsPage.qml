import QtQuick
import QtQuick.Controls
import DesktopPet

// Settings page (Wave 7 todo 15) — startup/exit behavior controls bound to
// PanelConfig via the PanelConfigController context property ("panelConfig").
//
// Four controls, all persisting on change:
//   1. closeAction SegmentedControl — "exit" / "minimize" (B2 guard: NOT
//      "hide_to_tray"). Read from panelConfig.closeAction (Q_PROPERTY),
//      written via panelConfig.closeAction = "exit"|"minimize".
//   2. confirmOnExit Checkbox — shows exit-confirm dialog on window close.
//   3. autoLaunchSystem Checkbox — toggles OS auto-launch via the
//      AutoLaunchManager context property ("autoLaunch"). Initialized from
//      the LIVE registry state (autoLaunch.isEnabled()), not the persisted
//      PanelConfig.autoLaunchSystem desire.
//   4. startMinimized Checkbox — launch with the window hidden.
//
// Theme tokens from Theme singleton (T26): bgColor, surfaceColor, textColor,
// accentColor. No hardcoded hex values.
Rectangle {
    id: root
    color: Theme.bgColor

    // Muted text color (alpha blend on Theme.textColor — same pattern as
    // WelcomePage / InstanceDetailPage for secondary text).
    readonly property color _mutedColor: {
        var c = Theme.textColor
        return Qt.rgba(c.r, c.g, c.b, 0.6)
    }

    Flickable {
        anchors.fill: parent
        anchors.topMargin: 32
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.bottomMargin: 24
        contentHeight: settingsColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: settingsColumn
            spacing: 28
            width: parent.width

            Text {
                text: qsTr("Settings")
                color: Theme.textColor
                font.pixelSize: 28
                font.weight: Font.DemiBold
            }

            // ── Section: Exit Behavior ──────────────────────────────────
            Column {
                spacing: 16
                width: parent.width

                Text {
                    text: qsTr("退出行为")
                    color: Theme.accentColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                Text {
                    text: qsTr("关闭按钮行为")
                    color: Theme.textColor
                    font.pixelSize: 14
                }

                // closeAction SegmentedControl — two toggle buttons. Active =
                // accentColor fill + bgColor text; inactive = surfaceColor +
                // textColor + muted border. B2 guard: values are "exit" /
                // "minimize" only (NOT "hide_to_tray").
                Row {
                    spacing: 2

                    Rectangle {
                        width: 160; height: 34
                        radius: 4
                        color: panelConfig.closeAction === "exit"
                               ? Theme.accentColor : Theme.surfaceColor
                        border.width: panelConfig.closeAction === "exit" ? 0 : 1
                        border.color: root._mutedColor

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("退出时关闭")
                            color: panelConfig.closeAction === "exit"
                                   ? Theme.bgColor : Theme.textColor
                            font.pixelSize: 13
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panelConfig.closeAction = "exit"
                        }
                    }

                    Rectangle {
                        width: 160; height: 34
                        radius: 4
                        color: panelConfig.closeAction === "minimize"
                               ? Theme.accentColor : Theme.surfaceColor
                        border.width: panelConfig.closeAction === "minimize" ? 0 : 1
                        border.color: root._mutedColor

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("最小化到托盘")
                            color: panelConfig.closeAction === "minimize"
                                   ? Theme.bgColor : Theme.textColor
                            font.pixelSize: 13
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panelConfig.closeAction = "minimize"
                        }
                    }
                }

                AppCheckBox {
                    id: confirmExitCb
                    text: qsTr("退出前确认")
                    checked: panelConfig.confirmOnExit
                    onToggled: panelConfig.confirmOnExit = checked
                }

                Text {
                    text: qsTr("勾选后，点击关闭按钮时会弹出确认对话框。")
                    color: root._mutedColor
                    font.pixelSize: 12
                    leftPadding: 36
                }
            }

            // ── Section: Startup ────────────────────────────────────────
            Column {
                spacing: 16
                width: parent.width

                Text {
                    text: qsTr("启动")
                    color: Theme.accentColor
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                // autoLaunchSystem Checkbox — initialized from the LIVE
                // registry state (autoLaunch.isEnabled()), not the persisted
                // desire. On toggle: call enable()/disable() + persist the
                // desire so the startup hook can re-arm if the registry was
                // cleared externally.
                AppCheckBox {
                    id: autoLaunchCb
                    text: qsTr("开机自启动")
                    Component.onCompleted: checked = autoLaunch.isEnabled()
                    onToggled: {
                        if (checked) {
                            autoLaunch.enable()
                            panelConfig.autoLaunchSystem = true
                        } else {
                            autoLaunch.disable()
                            panelConfig.autoLaunchSystem = false
                        }
                    }
                }

                AppCheckBox {
                    id: startMinimizedCb
                    text: qsTr("启动时最小化")
                    checked: panelConfig.startMinimized
                    onToggled: panelConfig.startMinimized = checked
                }

                Text {
                    text: qsTr("勾选后，程序启动时窗口将隐藏到系统托盘。")
                    color: root._mutedColor
                    font.pixelSize: 12
                    leftPadding: 36
                }
            }

            // ── Tray availability hint ──────────────────────────────────
            // Shown only when trayManager.isAvailable() returns false (GNOME-
            // without-extension / headless / CI). Explains why "最小化到托盘"
            // may not work. Hidden on Windows + KDE (the common case).
            Rectangle {
                width: parent.width
                height: trayHintCol.height + 24
                color: Theme.surfaceColor
                radius: 6
                visible: !trayManager.isAvailable()
                Column {
                    id: trayHintCol
                    anchors.centerIn: parent
                    spacing: 6
                    width: parent.width - 32
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("System tray not available")
                        color: root._mutedColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 32
                        text: qsTr("On Linux GNOME, install the AppIndicator or " +
                                   "KStatusNotifierItem shell extension. The close " +
                                   "button still works regardless.")
                        color: root._mutedColor
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
