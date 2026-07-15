import QtQuick

// Sidebar — instance list shell (T25 / Phase 4.3).
//
// Left-side panel (200px) listing the Live2D instances. Each row shows the
// instance label, its model name, and a status badge. An "Add Instance"
// button at the bottom opens the create flow — Phase 5 wires the real flow;
// for now the click just logs "add instance clicked".
//
// The list is bound to an empty ListModel. Phase 5 replaces this with a
// QAbstractListModel exposed from C++ (InstanceListModel in src/ui/).
//
// Parent must be the ApplicationWindow contentItem (same parent as StackView
// in Main.qml). Anchors itself to the left edge, below the 32px titlebar.
Rectangle {
    id: root

    // Geometry: pinned to the left edge, below the 32px titlebar.
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.topMargin: 32     // clear the custom titlebar (TitleBar.qml)
    width: 200

    // Catppuccin Mocha "mantle" — one shade darker than the content area
    // (#1e1e2e base) to visually separate the sidebar from the page body.
    color: "#181825"

    // ── Instance list model (empty — Phase 5 wires the real model) ────────
    ListModel { id: instanceModel }

    // ── Instance list ─────────────────────────────────────────────────────
    ListView {
        id: instanceList
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: addButton.top
        clip: true
        model: instanceModel

        // Row delegate: label + model name (left) + status badge (right).
        // All three are placeholder text until Phase 5 populates the model.
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
                    text: model.modelName
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
    // Phase 5 wires the real create flow; for now it just logs so the click
    // path is observable during shell development.
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
            onClicked: console.log("add instance clicked")
        }
    }
}
