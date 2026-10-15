import QtQuick
import DesktopPet

// AppButton — design-mock .btn variants: normal / primary / subtle / danger.
// Emits clicked(); text + optional leading vector icon (AppIcon). The icon
// replaces emoji glyph prefixes ("📂 打开…", "🗑 删除") that rendered as tofu
// on hosts without a color-emoji font; plain-symbol prefixes that render
// everywhere ("▶", "↻", "＋") stay text where they stand alone.
Rectangle {
    id: root

    property string text: ""
    property string style: "normal"   // normal | primary | subtle | danger
    property bool enabled: true
    property real fontSize: 13
    property string iconName: ""      // optional AppIcon key; "" = text-only
    property real iconSize: 14

    signal clicked()

    implicitWidth: _content.implicitWidth + 32
    implicitHeight: 32
    radius: Theme.radiusMd

    readonly property bool _primary: style === "primary"
    readonly property bool _subtle: style === "subtle"
    readonly property bool _danger: style === "danger"

    color: {
        if (_primary) {
            // FluFilledButton fill recipe (see Theme.accentHoverColor):
            // accent solid → hover steps 1.1 → press steps 1.2 further
            // (scheme-aware lighter/darker). Text stays accentTextColor;
            // the whole-button disabled opacity below keeps the dim.
            if (!enabled) return Theme.accentColor
            if (_tap.pressed) return Theme.accentPressedColor
            if (_hover.hovered) return Theme.accentHoverColor
            return Theme.accentColor
        }
        if (_subtle)
            return _hover.hovered && enabled
                ? Qt.alpha(Theme.textColor, 0.09)
                : Qt.alpha(Theme.textColor, 0.045)
        if (_danger)
            return _hover.hovered && enabled ? Theme.errorBgColor : "transparent"
        return _hover.hovered && enabled ? Theme.hoverColor : Theme.surfaceColor
    }
    border.width: _primary || _subtle ? 0 : 1
    border.color: _danger ? Qt.alpha(Theme.errorColor, 0.4)
                          : Qt.alpha(Theme.textColor, 0.18)

    Row {
        id: _content
        anchors.centerIn: parent
        spacing: 6

        AppIcon {
            name: root.iconName
            size: root.iconSize
            // icon follows the label color: on a primary fill that is
            // Theme.accentTextColor, so the pair always stays readable
            color: _label.color
            visible: root.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: _label
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            // Wave-2 fix (④): primary fill text used a hardcoded #ffffff —
            // accentTextColor (sys.highlightedText) pairs with the system
            // accent and auto-flips for light accents / dark scheme.
            color: {
                if (!root.enabled) return Theme.text3Color
                if (root._primary) return Theme.accentTextColor
                if (root._danger) return Theme.errorColor
                return Theme.textColor
            }
            font.pixelSize: root.fontSize
            font.weight: root._primary ? Font.DemiBold : Font.Normal
        }
    }

    HoverHandler { id: _hover; enabled: root.enabled }
    // TapHandler.pressed drives the primary fill's pressed step (Qt Quick
    // has no standalone PressHandler; the tap gesture already tracks it).
    TapHandler {
        id: _tap
        enabled: root.enabled
        onTapped: root.clicked()
    }

    opacity: enabled ? 1.0 : 0.55
    Behavior on opacity { NumberAnimation { duration: 100 } }
}
