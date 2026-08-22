import QtQuick
import DesktopPet

// ToggleSwitch — design-mock .toggle: 40×20 pill, accent when on.
// Emits toggled(checked); bind `checked` + act in onToggled.
Rectangle {
    id: root

    property bool checked: false
    property bool enabled: true

    signal toggled()

    width: 40
    height: 20
    radius: 10
    color: checked ? Theme.accentColor
                   : Theme.withAlpha(Theme.textColor, 0.35)

    Rectangle {
        id: knob
        width: 14
        height: 14
        radius: 7
        color: "#ffffff"
        y: 3
        x: root.checked ? root.width - width - 3 : 3
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler {
        enabled: root.enabled
        onTapped: {
            root.checked = !root.checked
            root.toggled()
        }
    }
}
