import QtQuick
import QtQuick.Shapes
import DesktopPet

// BubbleCard — one notification bubble in the top-right stream. Plain
// QtQuick + QtQuick.Shapes only (NO FluentUI, NO QtQuick.Effects):
// renders inside the always-on-top frameless BubbleStreamWindow.
// Style: milky-white translucent card (兽耳-style 乳白半透明), soft
// gradient shadows.
//
// WHY Shapes for shadows: MultiEffect always paints its source silhouette
// (no shadow-only mode), which sat under the translucent fill and read as
// opaque — shipped twice. Rect hard edges were rejected in review. A
// radial-gradient ellipse fakes a soft shadow with true translucency.
Item {
    id: root

    required property string avatar
    required property string name
    required property string text
    property string avatarFile: ""   // image URL; empty → glyph fallback
    // Stack position (0 = newest/top). Older bubbles fade progressively —
    // mimics the reference product's aging cue: fresh text reads solid,
    // then dims as newer bubbles push it down.
    property int stackIndex: 0

    signal clicked()

    width: parent ? parent.width : 340
    height: cardBody.height
    opacity: 0
    x: 24

    Component.onCompleted: entryAnim.start()

    ParallelAnimation {
        id: entryAnim
        NumberAnimation {
            target: root
            property: "opacity"
            to: 1
            duration: 220
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: root
            property: "x"
            to: 0
            duration: 220
            easing.type: Easing.OutCubic
        }
    }

    // Soft ambient shadow: a large radial-gradient ellipse fading to
    // transparent. RadialGradient center alpha ~0.16 → invisible edges.
    Shape {
        x: -36
        y: -20
        width: cardBody.width + 72
        height: cardBody.height + 76
        ShapePath {
            strokeColor: "transparent"
            strokeWidth: 0
            fillGradient: RadialGradient {
                centerX: width / 2; centerY: height / 2
                centerRadius: Math.max(width, height) / 2
                GradientStop { position: 0.0; color: Qt.rgba(0.05, 0.06, 0.12, 0.16) }
                GradientStop { position: 0.62; color: Qt.rgba(0.05, 0.06, 0.12, 0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(0.05, 0.06, 0.12, 0.0) }
            }
            // Ellipse via two arcs (SVG path syntax).
            PathSvg {
                path: {
                    const w = width, h = height
                    return `M 0 ${h / 2} A ${w / 2} ${h / 2} 0 1 0 ${w} ${h / 2} A ${w / 2} ${h / 2} 0 1 0 0 ${h / 2} Z`
                }
            }
        }
    }

    // Card fill follows the SYSTEM scheme (was hardcoded milky-white): base
    // color from the palette, alpha stepped down with stackIndex (newest =
    // most opaque). Border = faint text-color wash.
    Rectangle {
        id: cardBody
        width: root.width
        height: contentRow.implicitHeight + 2 * 12
        radius: 12
        border.width: 1
        border.color: Qt.alpha(Theme.textColor,
                               Math.max(0.12 - 0.02 * root.stackIndex, 0.04))
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.alpha(Theme.surfaceColor,
                                Math.max(0.96 - 0.10 * root.stackIndex, 0.30))
            }
            GradientStop {
                position: 1.0
                color: Qt.alpha(Theme.surfaceColor,
                                Math.max(0.90 - 0.10 * root.stackIndex, 0.28))
            }
        }
    }

    Row {
        id: contentRow
        x: 14
        y: 12
        width: cardBody.width - 2 * 14
        spacing: 10

        Item {
            id: avatarSlot
            width: 38
            height: 38
            y: 1

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                visible: root.avatarFile.length === 0
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(Theme.accentColor, 1.3) }
                    GradientStop { position: 1; color: Theme.accentColor }
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.35)
                }
            }
            Image {
                anchors.fill: parent
                visible: root.avatarFile.length > 0
                source: root.avatarFile
                fillMode: Image.PreserveAspectCrop
            }
            Text {
                anchors.centerIn: parent
                visible: root.avatarFile.length === 0
                         && root.avatar.length > 0
                text: root.avatar
                font.pixelSize: 18
            }
            // empty avatar → vector paw fallback (was the 🐾 emoji)
            AppIcon {
                anchors.centerIn: parent
                visible: root.avatarFile.length === 0
                         && root.avatar.length === 0
                name: "paw"
                size: 20
                color: Theme.textColor
            }
        }

        Column {
            id: textColumn
            width: parent.width - avatarSlot.width - parent.spacing
            spacing: 3

            Text {
                width: parent.width
                text: root.name
                color: Theme.accentColor
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.text
                color: Theme.textColor
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 1.45
                lineHeightMode: Text.ProportionalHeight
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
