import QtQuick

// Welcome / home page — T24 placeholder.
//
// Shown by default in Main.qml's StackView. Phase 5 replaces the placeholder
// body with the instance list + quick-start UI.
Rectangle {
    color: "#1e1e2e"   // Catppuccin Mocha "base"

    // Page heading (top-left, large).
    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 32
        anchors.leftMargin: 24
        text: qsTr("Welcome")
        color: "#cdd6f4"   // Mocha "text"
        font.pixelSize: 28
        font.weight: Font.DemiBold
    }

    // Centered placeholder body.
    Text {
        anchors.centerIn: parent
        text: qsTr("Phase 5 content goes here")
        color: "#a6adc8"   // Mocha "subtext0"
        font.pixelSize: 14
    }
}
