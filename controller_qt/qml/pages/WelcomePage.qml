import QtQuick
import QtQuick.Controls
import DesktopPet

// Welcome / home page — T27 (Phase 4.5).
//
// First-run landing page shown by Main.qml's StackView. Combines:
//   - App icon placeholder + title + subtitle + primary CTA button
//   - An environment-detection panel that probes six readiness items
//     (renderers, Qt runtime, resources, models, WS port) and renders each
//     as a "● Ready / ● Not found" row.
//
// The checker is a global QML context property "envChecker" set in main.cpp
// (EnvironmentChecker, T27). It is NOT instantiated from QML — every page
// shares the same instance. runChecks() is called once on Component.onCompleted
// and can be re-triggered by the "Re-check" button (idempotent).
//
// All colors bind to the Theme singleton (T26) so the page re-renders
// instantly on theme switch. Status dot colors are the Catppuccin Mocha
// green/red pair also used by Sidebar.qml's status badge (system-status
// indicators read the same green=good / red=bad in every theme).
Rectangle {
    id: root
    color: Theme.bgColor

    // Status colors — Theme semantic tokens (universal good/bad signals).
    readonly property color _readyColor: Theme.successColor
    readonly property color _notReadyColor: Theme.errorColor

    // Muted secondary text.
    readonly property color _mutedColor: Theme.mutedTextColor

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 40
            spacing: 28

            // ── Header: icon + title + subtitle ──────────────────────────
            Column {
                anchors.left: parent.left
                anchors.leftMargin: 32
                anchors.right: parent.right
                anchors.rightMargin: 32
                spacing: 14

                // App icon placeholder — accent-colored circle with a paw glyph.
                Rectangle {
                    width: 72
                    height: 72
                    radius: 36
                    color: Theme.accentColor

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("🐾")   // paw — placeholder for real icon asset
                        font.pixelSize: 34
                    }
                }

                Text {
                    text: qsTr("Desktop Pet Controller")
                    color: Theme.textColor
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                }

                Text {
                    text: qsTr("Create your first pet instance to get started")
                    color: root._mutedColor
                    font.pixelSize: 14
                }

                // Primary CTA — Phase 5 wires the real create flow; for now
                // the click is a no-op (logs only). Enabled regardless of env
                // state so the user can always click; the env panel below
                // communicates what is missing.
                Rectangle {
                    id: createButton
                    width: createBtnText.implicitWidth + 32
                    height: 38
                    radius: Theme.radiusMd
                    color: createArea.containsMouse
                           ? Qt.darker(Theme.accentColor, 1.12)
                           : Theme.accentColor

                    Text {
                        id: createBtnText
                        anchors.centerIn: parent
                        text: qsTr("Create First Instance")
                        color: Theme.bgColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    MouseArea {
                        id: createArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: console.log("create first instance clicked (Phase 5)")
                    }
                }
            }

            // ── Environment detection panel ──────────────────────────────
            Rectangle {
                id: panel
                anchors.left: parent.left
                anchors.leftMargin: 32
                anchors.right: parent.right
                anchors.rightMargin: 32
                height: panelContent.implicitHeight + 48
                radius: Theme.radiusLg
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                border.width: 1

                Column {
                    id: panelContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 20
                    spacing: 14

                    // Panel header: title (left) + re-check button (right).
                    Item {
                        width: parent.width
                        height: Math.max(headerTitle.implicitHeight, recheckBtn.height)

                        Text {
                            id: headerTitle
                            text: qsTr("Environment")
                            color: Theme.textColor
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Rectangle {
                            id: recheckBtn
                            width: recheckText.implicitWidth + 20
                            height: 26
                            radius: 4
                            color: recheckArea.containsMouse
                                   ? Theme.hoverColor
                                   : "transparent"
                            border.color: root._mutedColor
                            border.width: 1
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                id: recheckText
                                anchors.centerIn: parent
                                text: qsTr("↻ Re-check")
                                color: root._mutedColor
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: recheckArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: envChecker.runChecks()
                            }
                        }
                    }

                    // Six check rows. Each row binds directly to the matching
                    // envChecker property; the dot/detail re-evaluate on
                    // checksChanged automatically.
                    CheckRow {
                        width: parent.width
                        ready: envChecker.openglRendererReady
                        label: qsTr("OpenGL Renderer")
                        detail: envChecker.openglRendererReady
                                ? qsTr("Found: ") + envChecker.openglRendererPath
                                : qsTr("Not found")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.vulkanRendererReady
                        label: qsTr("Vulkan Renderer")
                        detail: envChecker.vulkanRendererReady
                                ? qsTr("Found: ") + envChecker.vulkanRendererPath
                                : qsTr("Not found")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.qtRuntimeReady
                        label: qsTr("Qt Runtime")
                        detail: qsTr("Qt ") + envChecker.qtVersion
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.resourcesReady
                        label: qsTr("Resources")
                        detail: envChecker.resourcesReady
                                ? qsTr("Found: ") + envChecker.modelsDirectory
                                : qsTr("Not found")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.modelsAvailable
                        label: qsTr("Models")
                        detail: envChecker.modelsAvailable
                                ? qsTr("%1 available: %2")
                                  .arg(envChecker.availableModels.length)
                                  .arg(envChecker.availableModels.join(", "))
                                : qsTr("No models found")
                    }
                    CheckRow {
                        width: parent.width
                        ready: envChecker.portBindable
                        label: qsTr("WS Port %1").arg(envChecker.port)
                        detail: envChecker.portBindable
                                ? qsTr("Bindable (free)")
                                : qsTr("In use — another process is listening")
                    }
                }
            }

            // ── Overall status line ──────────────────────────────────────
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 32
                text: envChecker.allReady
                      ? qsTr("● All checks passed — ready to launch.")
                      : qsTr("● Some checks failed — resolve the items above.")
                color: envChecker.allReady ? root._readyColor : root._notReadyColor
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }
    }

    // ── CheckRow component: status dot + label/detail ──────────────────────
    // Inline component (Qt 6.3+). A transparent container anchoring a colored
    // dot to the left and a label+detail Column to the right of it. Height
    // tracks the Column so multi-line details are not clipped.
    component CheckRow : Rectangle {
        id: rowItem
        property bool ready: false
        property string label: ""
        property string detail: ""

        color: "transparent"
        height: Math.max(dot.height, rowCol.implicitHeight)

        Rectangle {
            id: dot
            width: 10
            height: 10
            radius: 5
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.topMargin: 4   // nudge down to align with the label baseline
            color: rowItem.ready ? root._readyColor : root._notReadyColor
        }

        Column {
            id: rowCol
            anchors.left: dot.right
            anchors.leftMargin: 12
            anchors.right: parent.right

            Text {
                text: rowItem.label
                color: Theme.textColor
                font.pixelSize: 13
                font.weight: Font.Medium
            }
            Text {
                text: rowItem.detail
                color: root._mutedColor
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: rowCol.width
            }
        }
    }

    // Run the six probes once the page is ready. QML bindings take it from
    // there — checksChanged refreshes every dot/detail automatically.
    Component.onCompleted: envChecker.runChecks()
}
