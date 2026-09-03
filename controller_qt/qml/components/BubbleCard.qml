import QtQuick
import QtQuick.Shapes

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

    // Milky-white card fill (兽耳 style): milky white, base fully opaque;
    // a barely-there top highlight — no blue tint. Alpha steps down with
    // stackIndex (newest = opaque): 1.0 → −0.10 per slot (floor 0.30).
    Rectangle {
        id: cardBody
        width: root.width
        height: contentRow.implicitHeight + 2 * 12
        radius: 12
        border.width: 1
        border.color: Qt.rgba(1.0, 1.0, 1.0,
                              Math.max(0.75 - 0.12 * root.stackIndex, 0.15))
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.rgba(1.0, 0.995, 0.98,
                               Math.max(1.0 - 0.10 * root.stackIndex, 0.30))
            }
            GradientStop {
                position: 1.0
                color: Qt.rgba(0.985, 0.975, 0.96,
                               Math.max(0.97 - 0.10 * root.stackIndex, 0.30))
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
                    GradientStop { position: 0; color: "#8b8bf0" }
                    GradientStop { position: 1; color: "#5b5bd6" }
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
                text: root.avatar.length > 0 ? root.avatar : "🐾"
                font.pixelSize: 18
            }
        }

        Column {
            id: textColumn
            width: parent.width - avatarSlot.width - parent.spacing
            spacing: 3

            Text {
                width: parent.width
                text: root.name
                color: "#4A4AC2"
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.text
                color: "#2F2F33"
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
