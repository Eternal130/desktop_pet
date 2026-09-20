import QtQuick
import QtQuick.Controls
import DesktopPet

// CreateInstanceDialog — name + emoji avatar picker + model combo in a
// scrim dialog. Caller opens via open().
//
// Creation happens HERE, not in the callers' onAccepted handlers: the
// chosen model must reach instanceManager.createInstance's third
// parameter (empty string = legacy default-model behavior), and the two
// handler sites (InstanceDetailPage / WelcomePage) are outside this
// lane's write boundary. After a successful create this dialog replicates
// what those handlers did — select the new row, and switch to the
// instance page when the dialog was opened from the welcome page.
// The legacy accepted(name, avatar) signal stays DECLARED (unemitted) so
// the existing onAccepted bindings keep binding cleanly; do not emit it,
// that would double-create.
Rectangle {
    id: root

    signal accepted(string name, string avatar)

    readonly property var _avatarChoices: [
        "🐱", "🐶", "🐰", "🐺", "🦊", "🐼", "🐻", "🐯",
        "🦁", "🐮", "🐷", "🐸", "🐧", "🐦", "🐤", "🦉",
    ]

    property string _selectedAvatar: "🐱"
    property alias nameText: _nameInput.text

    function _modelNames() {
        const out = []
        for (let i = 0; i < modelLibrary.modelCount; ++i)
            out.push(modelLibrary.modelDirName(i))
        return out
    }

    function open() {
        _nameInput.text = qsTr("宠物 %1").arg(instanceManager.rowCount() + 1)
        _selectedAvatar = "🐱"
        // Model combo: fresh options each open, preselected to the global
        // default (panelConfig.defaultModelName), falling back to the
        // first known model — never a stale pick from a previous open.
        const names = root._modelNames()
        _modelCombo.model = names
        const def = names.indexOf(panelConfig.defaultModelName)
        _modelCombo.currentIndex = def >= 0 ? def
                                            : (names.length > 0 ? 0 : -1)
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

            Text {
                text: qsTr("模型")
                color: Theme.text2Color
                font.pixelSize: 12
            }
            AppComboBox {
                id: _modelCombo
                width: parent.width
                model: []
                // -1 shows the placeholder until open() preselects the
                // global default; picking here overrides it per-instance.
                currentIndex: -1
            }
            Text {
                text: _modelCombo.currentIndex >= 0
                      ? qsTr("该模型将作为此实例的初始模型")
                      : qsTr("未选择模型时使用渲染器默认模型")
                color: Theme.text3Color
                font.pixelSize: 11
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
                        // Pass the chosen model as the third parameter
                        // (empty string = legacy default-model behavior);
                        // see the header comment for why creation happens
                        // here instead of the callers' onAccepted.
                        const model = _modelCombo.currentIndex >= 0
                                      ? _modelCombo.currentText : ""
                        const uuid = instanceManager.createInstance(
                            _nameInput.text.trim(),
                            root._selectedAvatar,
                            model)
                        root.close()
                        if (uuid !== "") {
                            const row = instanceManager.rowCount() - 1
                            Window.window.selectInstance(
                                row,
                                instanceManager.instanceAt(row).instanceId)
                            if (Window.window.currentPage === "welcome")
                                Window.window.switchPage("instance")
                        }
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
