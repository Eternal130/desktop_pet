import QtQuick
import DesktopPet

// AppDialog — design-mock floating confirmation dialog with scrim.
// open()/close(); positiveClicked / negativeClicked signals.
Rectangle {
    id: root

    property string title: ""
    property string message: ""
    property string positiveText: qsTr("确定")
    property string negativeText: qsTr("取消")
    property bool showNegative: true

    signal positiveClicked()
    signal negativeClicked()

    function open()  { root.visible = true }
    function close() { root.visible = false }

    visible: false
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.35)
    z: 100

    // scrim first (declared before the card = below it in stacking order;
    // a trailing MouseArea here used to sit ON TOP of the card and swallow
    // every button click)
    MouseArea { anchors.fill: parent; onClicked: {} }

    Rectangle {
        anchors.centerIn: parent
        width: 360
        height: _col.implicitHeight + 48
        radius: Theme.radiusLg
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.borderColor

        Column {
            id: _col
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 16

            Text {
                width: parent.width
                text: root.title
                color: Theme.textColor
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: root.message
                color: Theme.text2Color
                font.pixelSize: 13
            }
            Row {
                spacing: 10
                layoutDirection: Qt.RightToLeft
                AppButton {
                    style: "primary"
                    text: root.positiveText
                    onClicked: {
                        root.positiveClicked()
                        root.close()
                    }
                }
                AppButton {
                    visible: root.showNegative
                    text: root.negativeText
                    onClicked: {
                        root.negativeClicked()
                        root.close()
                    }
                }
            }
        }
    }
}
