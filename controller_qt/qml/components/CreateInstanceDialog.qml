import QtQuick
import QtQuick.Controls
import DesktopPet

// CreateInstanceDialog — name + emoji avatar picker in a scrim dialog.
// Caller opens via open(); onAccepted carries (name, avatar). Empty name is
// rejected (button disabled) — creation always has a user-visible label.
Rectangle {
    id: root

    signal accepted(string name, string avatar)

    readonly property var _avatarChoices: [
        "🐱", "🐶", "🐰", "🐺", "🦊", "🐼", "🐻", "🐯",
        "🦁", "🐮", "🐷", "🐸", "🐧", "🐦", "🐤", "🦉",
    ]

    property string _selectedAvatar: "🐱"
    property alias nameText: _nameInput.text

    function open() {
        _nameInput.text = qsTr("宠物 %1").arg(instanceManager.rowCount() + 1)
        _selectedAvatar = "🐱"
        root.visible = true
        _nameInput.forceActiveFocus()
        _nameInput.selectAll()
    }
    function close() { root.visible = false }

    visible: false
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.35)
    z: 100

    MouseArea { anchors.fill: parent; onClicked: {} }

    Rectangle {
        anchors.centerIn: parent
        width: 400
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
            spacing: 14

            Text {
                text: qsTr("创建实例")
                color: Theme.textColor
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            Text {
                text: qsTr("名称")
                color: Theme.text2Color
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 34
                radius: Theme.radiusMd
                color: Theme.dark ? "#262626" : "#fafafa"
                border.width: 1
                border.color: _nameInput.activeFocus ? Theme.accentColor : Theme.borderColor

                TextInput {
                    id: _nameInput
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: Theme.textColor
                    font.pixelSize: 13
                    onAccepted: if (_createBtn.enabled) _createBtn.clicked()
                    onActiveFocusChanged: if (activeFocus) selectAll()
                }
            }

            Text {
                text: qsTr("头像")
                color: Theme.text2Color
                font.pixelSize: 12
            }
            GridView {
                width: parent.width
                height: 4 * 40 + 3 * 6
                cellWidth: width / 4
                cellHeight: 40
                clip: true
                interactive: false
                model: root._avatarChoices
                delegate: Rectangle {
                    width: GridView.view.cellWidth - 6
                    height: GridView.view.cellHeight - 6
                    radius: Theme.radiusMd
                    color: root._selectedAvatar === modelData
                           ? Theme.accentAlpha(0.14) : "transparent"
                    border.width: root._selectedAvatar === modelData ? 1 : 0
                    border.color: Theme.accentColor
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 20
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root._selectedAvatar = modelData
                    }
                }
            }

            Row {
                spacing: 10
                layoutDirection: Qt.RightToLeft
                AppButton {
                    id: _createBtn
                    style: "primary"
                    text: qsTr("创建")
                    enabled: _nameInput.text.trim().length > 0
                    onClicked: {
                        root.accepted(_nameInput.text.trim(), root._selectedAvatar)
                        root.close()
                    }
                }
                AppButton {
                    text: qsTr("取消")
                    onClicked: root.close()
                }
            }
        }
    }
}
