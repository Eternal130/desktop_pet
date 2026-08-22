import QtQuick
import DesktopPet

// AppSlider — design-mock .slider-row: 4px track, 18px accent-ringed knob.
// Emits moved() with the new value in `value` (snap to stepSize).
Rectangle {
    id: root

    property real from: 0
    property real to: 1
    property real stepSize: 0.05
    property real value: from
    property bool enabled: true

    signal moved(real val)

    width: 200
    height: 22
    color: "transparent"

    function _frac(v) {
        return (v - from) / (to - from)
    }

    Rectangle {
        // full track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 2
        color: Theme.withAlpha(Theme.textColor, 0.12)
    }
    Rectangle {
        // filled portion
        anchors.verticalCenter: parent.verticalCenter
        x: 0
        width: Math.max(0, Math.min(parent.width, root._frac(root.value) * parent.width))
        height: 4
        radius: 2
        color: root.enabled ? Theme.accentColor : Theme.text3Color
    }
    Rectangle {
        // knob: white disc with 5px accent ring (design mock .knob)
        x: Math.max(0, Math.min(root.width - width,
                                root._frac(root.value) * root.width - width / 2))
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: 9
        color: "#ffffff"
        border.width: 5
        border.color: root.enabled ? Theme.accentColor : Theme.text3Color
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        enabled: root.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        function apply(px) {
            const f = Math.max(0, Math.min(1, px / root.width))
            const raw = root.from + f * (root.to - root.from)
            const snapped = Math.round(raw / root.stepSize) * root.stepSize
            const v = Math.max(root.from, Math.min(root.to, snapped))
            if (v !== root.value) {
                root.value = v
                root.moved(v)
            }
        }
        onPressed: (mouse) => apply(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) apply(mouse.x) }
    }
}
