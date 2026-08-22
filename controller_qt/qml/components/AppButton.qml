import QtQuick
import DesktopPet

// AppButton — design-mock .btn variants: normal / primary / subtle / danger.
// Emits clicked(); text-only control.
Rectangle {
    id: root

    property string text: ""
    property string style: "normal"   // normal | primary | subtle | danger
    property bool enabled: true
    property real fontSize: 13

    signal clicked()

    implicitWidth: _label.implicitWidth + 32
    implicitHeight: 32
    radius: Theme.radiusMd

    readonly property bool _primary: style === "primary"
    readonly property bool _subtle: style === "subtle"
    readonly property bool _danger: style === "danger"

    color: {
        if (_primary)
            return _hover.hovered && enabled ? Theme.accentHoverColor : Theme.accentColor
        if (_subtle)
            return _hover.hovered && enabled
                ? Theme.withAlpha(Theme.textColor, Theme.dark ? 0.10 : 0.07)
                : Theme.withAlpha(Theme.textColor, Theme.dark ? 0.06 : 0.03)
        if (_danger)
            return _hover.hovered && enabled ? Theme.errorBgColor : "transparent"
        return _hover.hovered && enabled ? Theme.hoverColor : Theme.surfaceColor
    }
    border.width: _primary || _subtle ? 0 : 1
    border.color: _danger ? Theme.withAlpha(Theme.errorColor, 0.4)
                          : Theme.dark ? "#ffffff2e" : "#00000038"

    Text {
        id: _label
        anchors.centerIn: parent
        text: root.text
        color: {
            if (!root.enabled) return Theme.text3Color
            if (root._primary) return "#ffffff"
            if (root._danger) return Theme.errorColor
            return Theme.textColor
        }
        font.pixelSize: root.fontSize
        font.weight: root._primary ? Font.DemiBold : Font.Normal
    }

    HoverHandler { id: _hover; enabled: root.enabled }
    TapHandler {
        enabled: root.enabled
        onTapped: root.clicked()
    }

    opacity: enabled ? 1.0 : 0.55
    Behavior on opacity { NumberAnimation { duration: 100 } }
}
