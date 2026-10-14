import QtQuick
import DesktopPet

// AppDialog — design-mock floating confirmation dialog with scrim.
// open()/close(); positiveClicked / neutralClicked / negativeClicked signals.
//
// Neutral action (2026-10-15): optional third button for three-way
// confirmations (tray-app close: 最小化到托盘 / 退出 / 取消). Declared via
// neutralText ("" hides the button — every pre-existing call site keeps its
// exact two-button layout). Button order in the right-to-left row:
// positive (rightmost) · neutral · negative (leftmost). Style: the neutral
// action is the non-destructive PRIMARY accent button when the dialog is
// danger-mode (positive renders red), plain otherwise.
//
// P1-8 (true modal): appModal (default true) — open() re-parents the
// dialog to the WINDOW's contentItem so the scrim + card cover the whole
// window (titlebar + nav included); nav clicks can no longer silently
// drop a pending confirmation, and z:100 also sits above the resize
// edges (z 50). The re-parent is guarded, so dialogs already declared at
// window level (Main.qml's delete/exit confirms are contentItem children)
// take the no-op path — no double nesting. appModal: false keeps the
// legacy page-local scrim. `anchors.fill: parent` re-evaluates after the
// re-parent, so no extra coordinate plumbing is needed.
//
// P2-10 (keyboard): open() focuses the dialog root; Esc triggers the
// negative action (plain close when showNegative hides the button —
// hidden buttons must not "click"), Enter/Return trigger the positive
// one. AppDialog content is plain text (no input items), so root-level
// Keys can never shadow typing in a text field.
Rectangle {
    id: root

    property string title: ""
    property string message: ""
    property string positiveText: qsTr("确定")
    property string neutralText: ""  // optional third action ("" = hidden)
    property string negativeText: qsTr("取消")
    property bool showNegative: true
    property bool danger: false      // positive button renders as "danger"
    property bool appModal: true     // P1-8: window-level scrim (see header)

    signal positiveClicked()
    signal neutralClicked()
    signal negativeClicked()

    function open() {
        if (appModal) {
            const w = root.Window.window
            if (w && root.parent !== w.contentItem)
                root.parent = w.contentItem
        }
        root.visible = true
        root.forceActiveFocus()
    }
    function close() { root.visible = false }

    visible: false
    anchors.fill: parent
    color: Theme.scrimColor
    z: 100

    Keys.onEscapePressed: {
        if (root.showNegative)
            root.negativeClicked()
        root.close()
    }
    Keys.onReturnPressed: {
        root.positiveClicked()
        root.close()
    }
    Keys.onEnterPressed: {
        root.positiveClicked()
        root.close()
    }

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
                    style: root.danger ? "danger" : "primary"
                    text: root.positiveText
                    onClicked: {
                        root.positiveClicked()
                        root.close()
                    }
                }
                AppButton {
                    visible: root.neutralText.length > 0
                    // complementary accent for danger dialogs (退出红 +
                    // 最小化到托盘主蓝), plain elsewhere
                    style: root.danger ? "primary" : "normal"
                    text: root.neutralText
                    onClicked: {
                        root.neutralClicked()
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
