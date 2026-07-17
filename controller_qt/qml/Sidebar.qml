import QtQuick
import QtQuick.Controls
import DesktopPet

// Sidebar — instance list (T25 shell + Phase 5 todo 7 wiring).
//
// Left-side panel (200px) listing the Live2D instances. Each row shows the
// instance label, its model name, and a status badge. An "Add Instance"
// button at the bottom opens a Dialog prompting for a label, then calls
// instanceManager.createInstance(label). Clicking a row emits
// instanceSelected(row, uuid) — Main.qml wires that to swap the detail page.
//
// The list is bound to the `instanceManager` context property (a
// QAbstractListModel exposed from main.cpp). Delegate roles: label, modelName,
// status, connected, uuid (InstanceManager::roleNames, todo 3).
//
// Parent must be the ApplicationWindow contentItem (same parent as StackView
// in Main.qml). Anchors itself to the left edge, below the 32px titlebar.
Rectangle {
    id: root

    // Emitted when a row is clicked. Carries the row index + the instance UUID
    // (the latter is not reachable via InstanceSession from QML — config() is
    // not Q_INVOKABLE — so we surface it here from the model's uuid role).
    // Main.qml connects this to its selectInstance(row, uuid) handler.
    signal instanceSelected(int row, string uuid)

    // Geometry: pinned to the left edge, below the 32px titlebar.
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.topMargin: 32     // clear the custom titlebar (TitleBar.qml)
    width: 200

    // Catppuccin Mocha "mantle" — one shade darker than the content area
    // (#1e1e2e base) to visually separate the sidebar from the page body.
    color: "#181825"

    // ── Instance list ─────────────────────────────────────────────────────
    ListView {
        id: instanceList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: addButton.top
        clip: true
        // The context property `instanceManager` is a QAbstractListModel
        // (InstanceManager.hpp). rowsInserted / rowsRemoved propagate to the
        // ListView automatically — createInstance / deleteInstance fire them.
        model: instanceManager

        // Row delegate: label + model name (left) + status badge (right).
        // model.label/modelName/status/connected/uuid come from the roleNames
        // declared in InstanceManager::roleNames().
        delegate: Rectangle {
            width: instanceList.width
            height: 56
            color: rowArea.containsMouse ? "#313244" : "transparent"   // Mocha "surface0" on hover

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    text: model.label
                    color: "#cdd6f4"   // Mocha "text"
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                Text {
                    text: model.modelName || qsTr("(no model)")
                    color: "#a6adc8"   // Mocha "subtext0"
                    font.pixelSize: 11
                }
            }

            // Status badge — right-aligned pill. Color hints at state:
            // green-tinted for "running", neutral surface0 otherwise.
            Rectangle {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: statusText.implicitWidth + 16
                height: 20
                radius: 10
                color: model.status === "running" ? "#1e3a2f" : "#313244"

                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: model.status
                    color: model.status === "running" ? "#a6e3a1" : "#a6adc8"   // Mocha "green" / subtext0
                    font.pixelSize: 10
                    font.capitalization: Font.AllUppercase
                }
            }

            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.instanceSelected(model.index, model.uuid)
            }
        }

        // Empty state — shown when the model has zero rows.
        Text {
            anchors.centerIn: parent
            visible: instanceList.count === 0
            text: qsTr("No instances")
            color: "#a6adc8"   // Mocha "subtext0"
            font.pixelSize: 13
        }
    }

    // ── Add Instance button (bottom-pinned, full-width) ──────────────────
    // Opens addInstanceDialog (below) prompting for a label; on accept calls
    // instanceManager.createInstance(label). Empty label defaults to "新实例".
    Rectangle {
        id: addButton
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: addArea.containsMouse ? "#313244" : "transparent"

        // Top hairline border separating the button from the list above.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: "#313244"   // Mocha "surface0"
        }

        Text {
            anchors.centerIn: parent
            text: qsTr("\uFF0B Add Instance")   // ＋ full-width plus
            color: "#cdd6f4"   // Mocha "text"
            font.pixelSize: 13
            font.weight: Font.Medium
        }

        MouseArea {
            id: addArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                addLabelField.text = addInstanceDialog.defaultLabel
                addInstanceDialog.open()
            }
        }
    }

    // ── Add Instance dialog ──────────────────────────────────────────────
    // QtQuick.Controls Dialog with a TextField. Empty/whitespace label
    // collapses to the default "新实例" (per the acceptance criterion). The
    // TextField gets initial focus so the user can start typing immediately;
    // Enter submits via onAccepted.
    Dialog {
        id: addInstanceDialog
        anchors.centerIn: parent
        modal: true
        focus: true
        title: qsTr("Add Instance")
        standardButtons: Dialog.Cancel | Dialog.Ok
        width: 320

        // Pre-filled default; reset each time the button is clicked.
        property string defaultLabel: qsTr("新实例")

        onOpened: addLabelField.forceActiveFocus()

        onAccepted: {
            var label = addLabelField.text.trim()
            if (label.length === 0)
                label = defaultLabel
            instanceManager.createInstance(label)
        }

        contentItem: Column {
            spacing: 12
            Text {
                text: qsTr("Instance label:")
                color: Theme.textColor
                font.pixelSize: 12
            }
            TextField {
                id: addLabelField
                width: parent.width
                focus: true
                selectByMouse: true
                color: Theme.textColor
                font.pixelSize: 13
                text: addInstanceDialog.defaultLabel
                onAccepted: addInstanceDialog.accept()
            }
        }
    }
}
