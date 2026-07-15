import QtQuick

// Monitor page — T24 placeholder.
//
// Will show the live WS message log + renderer process metrics (Phase 5+).
Rectangle {
    color: "#1e1e2e"   // Catppuccin Mocha "base"

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 32
        anchors.leftMargin: 24
        text: qsTr("Monitor")
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
