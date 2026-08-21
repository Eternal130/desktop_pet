import QtQuick
import QtQuick.Controls
import FluentUI
import DesktopPet

// Welcome / home page — FluentUI rewrite (feat/qt-fluentui-rewrite branch).
//
// Same data model as the classic page: envChecker context property probes
// six readiness items; this version renders them inside a FluCard with
// FluentUI buttons/typography. CheckRow stays a custom component (dot +
// label/detail) since FluentUI has no direct equivalent.
Rectangle {
    id: root
    color: "transparent"

    readonly property color _readyColor: Theme.successColor
    readonly property color _notReadyColor: Theme.errorColor
    readonly property color _mutedColor: Theme.mutedTextColor

    Flickable {
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentHeight: contentColumn.implicitHeight + 48
        ScrollBar.vertical: FluScrollBar {}

        Column {
            id: contentColumn
            width: root.width - 64
            x: 32
            spacing: 28
            topPadding: 40

            // ── Header: icon + title + subtitle + CTA ───────────────────
            Column {
                spacing: 14

                Rectangle {
                    width: 72
                    height: 72
                    radius: 36
                    color: Theme.accentColor

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("🐾")
                        font.pixelSize: 34
                    }
                }

                FluText {
                    text: qsTr("Desktop Pet Controller")
                    font: FluTextStyle.Title
                }

                FluText {
                    text: qsTr("Create your first pet instance to get started")
                    color: root._mutedColor
                    font: FluTextStyle.Caption
                }

                FluFilledButton {
                    id: createButton
                    text: qsTr("Create First Instance")
                    implicitWidth: 220
                    implicitHeight: 40
                    onClicked: console.log("create first instance clicked")
                }
            }

            // ── Environment detection panel ──────────────────────────────
            FluFrame {
                width: parent.width
                padding: 20

                Column {
                    id: panelContent
                    width: parent.width
                    spacing: 14

                    Item {
                        width: parent.width
                        height: Math.max(headerTitle.implicitHeight, recheckBtn.height)

                        FluText {
                            id: headerTitle
                            text: qsTr("Environment")
                            font: FluTextStyle.BodyStrong
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        FluButton {
                            id: recheckBtn
                            text: qsTr("↻ Re-check")
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: envChecker.runChecks()
                        }
                    }

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

            FluText {
                text: envChecker.allReady
                      ? qsTr("● All checks passed — ready to launch.")
                      : qsTr("● Some checks failed — resolve the items above.")
                color: envChecker.allReady ? root._readyColor : root._notReadyColor
                font.pixelSize: 13
            }
        }
    }

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
            anchors.topMargin: 4
            color: rowItem.ready ? root._readyColor : root._notReadyColor
        }

        Column {
            id: rowCol
            anchors.left: dot.right
            anchors.leftMargin: 12
            anchors.right: parent.right

            FluText {
                text: rowItem.label
                font: FluTextStyle.BodyStrong
            }
            FluText {
                text: rowItem.detail
                color: root._mutedColor
                font: FluTextStyle.Caption
                wrapMode: Text.WordWrap
                width: rowCol.width
            }
        }
    }

    Component.onCompleted: envChecker.runChecks()
}
