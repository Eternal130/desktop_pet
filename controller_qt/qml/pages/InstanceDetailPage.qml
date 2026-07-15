import QtQuick

// Instance detail page — T24 placeholder.
//
// Will host the per-instance model/motion/scale controls once Phase 5 lands
// the real InstanceController backing.
Rectangle {
    color: "#1e1e2e"   // Catppuccin Mocha "base"

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 32
        anchors.leftMargin: 24
        text: qsTr("Instance Detail")
        color: "#cdd6f4"   // Mocha "text"
        font.pixelSize: 28
        font.weight: Font.DemiBold
    }

    Text {
        anchors.centerIn: parent
        text: qsTr("Phase 5 content goes here")
        color: "#a6adc8"   // Mocha "subtext0"
        font.pixelSize: 14
    }
}
