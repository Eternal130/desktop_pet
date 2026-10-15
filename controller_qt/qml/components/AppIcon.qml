import QtQuick
import QtQuick.Shapes
import DesktopPet

// AppIcon — inline vector icon set (Fluent-line style), the replacement for
// every emoji "icon" the panel used to inline in text (nav 🏠🐱📊, buttons
// 🗑📂, logo 🐾, avatar data 🐱🐶…). Emoji glyphs depend on a color-emoji
// font the QA/runtime hosts may not have (they rendered as tofu boxes) and
// they cannot follow the theme, so icons are now stroked SVG paths drawn
// directly with QtQuick.Shapes — no font, no image files, no new C++.
//
// Usage:
//   AppIcon { name: "home"; size: 16; color: Theme.text2Color }
//   // avatar DATA (emoji strings stored in instance configs) keeps rendering
//   // through the same component via `glyph`, which maps known emoji to the
//   // icon table and falls back to plain text for unknown/custom strings:
//   AppIcon { glyph: instance.avatar; size: 28 }
//
// Path coordinates live on a fixed 24×24 grid (stroke 1.7, round caps/joins)
// and the Shape is scaled to `size`, so proportions match at every use site.
// `color` defaults to Theme.textColor — bind a Theme token so the icon
// follows the light/dark scheme like any other surface.
Item {
    id: root

    property string name: ""       // icon table key ("home", "trash", …)
    property string glyph: ""      // legacy emoji/data string → mapped icon
    property color color: Theme.textColor
    property real size: 16
    // Optical stroke weight on the 24×24 design grid. The default 1.7 fits
    // the Fluent line-icon set at 16px; the window caption glyphs
    // (windowmin/max/restore/close) render at 10px where the native
    // Segoe Fluent Icons weight is 1px — pass 2.4 at those use sites.
    property real strokeWidth: 1.7

    implicitWidth: size
    implicitHeight: size

    // ── Icon table (24×24 stroked paths) ────────────────────────────────
    // Grouped: UI glyphs first, then the animal avatar set backing the
    // CreateInstanceDialog picker (the picker still STORES the emoji as
    // data — only the rendering went vector).
    readonly property var _paths: ({
        // UI
        home: "M3.8 10.6 12 3.6l8.2 7 M5.9 9.6v9.3a1.6 1.6 0 0 0 1.6 1.6h9a1.6 1.6 0 0 0 1.6-1.6V9.6 M9.9 20.5v-4.4a1.5 1.5 0 0 1 1.5-1.5h1.2a1.5 1.5 0 0 1 1.5 1.5v4.4",
        chart: "M4.5 4.5v15h15 M7.5 14.8l3.5-4.1 3 2.6 4.2-5.3",
        mic: "M12 3.2a3.1 3.1 0 0 1 3.1 3.1v4.9a3.1 3.1 0 0 1-6.2 0V6.3A3.1 3.1 0 0 1 12 3.2z M6.3 11.2a5.7 5.7 0 0 0 11.4 0 M12 16.9v3.1 M8.8 20h6.4",
        cube: "M12 3.3 20 7.8v8.6l-8 4.5-8-4.5V7.8l8-4.5z M4.3 8l7.7 4.4L19.7 8 M12 12.4v8.3",
        folder: "M3.7 17V7a1.8 1.8 0 0 1 1.8-1.8h3.7L11.5 8h7A1.8 1.8 0 0 1 20.3 9.8V17a1.8 1.8 0 0 1-1.8 1.8H5.5A1.8 1.8 0 0 1 3.7 17z",
        folderopen: "M3.6 17.8V6.5a1.7 1.7 0 0 1 1.7-1.7h3.9l2.2 2.5h6.9A1.7 1.7 0 0 1 20 9v1.2 M3.6 17.8l2.3-5.5a1.7 1.7 0 0 1 1.6-1.1h12.4a1 1 0 0 1 .9 1.4l-2.2 5.2a1.7 1.7 0 0 1-1.6 1.1H3.6z",
        puzzle: "M9.8 5a1.9 1.9 0 0 1 3.8 0v.9h2.5A1.9 1.9 0 0 1 18 7.8v2.5h1a1.9 1.9 0 0 1 0 3.8h-1v2.5a1.9 1.9 0 0 1-1.9 1.9h-2.5v-1a1.9 1.9 0 0 0-3.8 0v1H7.3A1.9 1.9 0 0 1 5.4 16.6v-2.5h-1a1.9 1.9 0 0 1 0-3.8h1V7.8A1.9 1.9 0 0 1 7.3 5.9h2.5V5z",
        gear: "M12.13 2.40L16.69 3.62L17.02 6.84L17.16 6.98L20.38 7.31L21.60 11.87L18.98 13.77L18.93 13.95L20.25 16.91L16.91 20.25L13.95 18.93L13.77 18.98L11.87 21.60L7.31 20.38L6.98 17.16L6.84 17.02L3.62 16.69L2.40 12.13L5.02 10.23L5.07 10.05L3.75 7.09L7.09 3.75L10.05 5.07L10.23 5.02Z M12 8.9a3.1 3.1 0 1 0 .01 6.2a3.1 3.1 0 1 0-.01-6.2z",
        search: "M10.8 4.3a6.5 6.5 0 1 0 .01 13a6.5 6.5 0 1 0-.01-13z M15.5 15.5l4.7 4.7",
        paw: "M4.1 10.9a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M8.1 7.6a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M12.9 7.6a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M16.9 10.9a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M12 11.2c2.9 0 5.2 1.9 5.2 4.2 0 1.9-1.4 3.4-3.3 3.4-.8 0-1.3-.3-1.9-.3s-1.1.3-1.9.3c-1.9 0-3.3-1.5-3.3-3.4 0-2.3 2.3-4.2 5.2-4.2z",
        trash: "M4.8 6.7h14.4 M9.7 6.7V5.4a1.6 1.6 0 0 1 1.6-1.6h1.4a1.6 1.6 0 0 1 1.6 1.6v1.3 M6.6 6.9l.7 11.7a1.9 1.9 0 0 0 1.9 1.8h5.6a1.9 1.9 0 0 0 1.9-1.8l.7-11.7 M10.1 10.4v6.1 M13.9 10.4v6.1",
        brush: "M19.6 4.4c.8.8.7 2.1-.2 3l-6.4 6.4-2.8-2.8 6.4-6.4c.9-.9 2.2-1 3-.2z M9.4 11.8c-1.6.4-2.7 1.7-2.9 3.3-.2 1.3-.8 2.1-1.9 2.6 1 .7 2.3 1.1 3.6 1.1 2.2 0 4-1.6 4.2-3.7l-3-3.3z",
        eye: "M12 5.9c-4.3 0-7.6 3.3-8.9 6.1 1.3 2.8 4.6 6.1 8.9 6.1s7.6-3.3 8.9-6.1C19.6 9.2 16.3 5.9 12 5.9z M12 9.5a2.6 2.6 0 1 0 .01 5.2a2.6 2.6 0 1 0-.01-5.2z",
        upload: "M12 15.4V4.9 M8.4 8.5 12 4.9l3.6 3.6 M4.8 14.6v3.5a1.7 1.7 0 0 0 1.7 1.7h11a1.7 1.7 0 0 0 1.7-1.7v-3.5",
        shuffle: "M3.5 7.6h2.8c5.4 0 6 8.8 11.4 8.8h1.9 M3.5 16.4h2.8c1.7 0 3-.9 4-2.1 M20.5 7.6h-2.8c-1.7 0-3 .9-4 2.1 M18.2 5.3l2.3 2.3-2.3 2.3 M18.2 14.1l2.3 2.3-2.3 2.3",
        rocket: "M12 3.1c2.2 1.6 3.4 4.1 3.4 6.8 0 3-1.4 5.7-3.4 7.4-2-1.7-3.4-4.4-3.4-7.4 0-2.7 1.2-5.2 3.4-6.8z M12 7.7a1.7 1.7 0 1 0 .01 3.4a1.7 1.7 0 1 0-.01-3.4z M8.7 13.2c-1.8.8-2.9 2.4-3.1 4.4l3.2-1.3 M15.3 13.2c1.8.8 2.9 2.4 3.1 4.4l-3.2-1.3 M12 18.6v2.3",
        palette: "M12 3.6a8.4 8.4 0 1 0 .01 16.8c1.3 0 2.1-.7 2.1-1.7 0-.8-.5-1.3-.5-2 0-.9.7-1.6 1.8-1.6h1.5a3.6 3.6 0 0 0 3.6-3.6c0-4.4-3.9-7.9-8.5-7.9z M7.7 10.6v.01 M10.9 7.7v.01 M14.9 8.4v.01 M16.7 11.4v.01",
        info: "M12 3.7a8.3 8.3 0 1 0 .01 16.6A8.3 8.3 0 0 0 12 3.7z M12 11.2v4.7 M12 8v.01",
        play: "M8.4 6v12c0 .95 1.04 1.52 1.83 1.02l9.4-6a1.2 1.2 0 0 0 0-2.03l-9.4-6C9.44 4.49 8.4 5.06 8.4 6z",
        stop: "M8.2 6.9h7.6a1.3 1.3 0 0 1 1.3 1.3v7.6a1.3 1.3 0 0 1-1.3 1.3H8.2a1.3 1.3 0 0 1-1.3-1.3V8.2a1.3 1.3 0 0 1 1.3-1.3z",
        restart: "M4.6 8.3a7.7 7.7 0 0 1 14.4 0 M19.3 4v4.3H15 M19.4 15.7a7.7 7.7 0 0 1-14.4 0 M4.7 20v-4.3H9",
        copy: "M9.3 10.7h7.3a1.9 1.9 0 0 1 1.9 1.9v6.5a1.9 1.9 0 0 1-1.9 1.9H9.3a1.9 1.9 0 0 1-1.9-1.9v-6.5a1.9 1.9 0 0 1 1.9-1.9z M14.7 7.4V7a1.9 1.9 0 0 0-1.9-1.9H6.5A1.9 1.9 0 0 0 4.6 7v6.3a1.9 1.9 0 0 0 1.9 1.9h.6",
        // nav-pane hamburger (Fluent NavigationView global-nav glyph):
        // three full-width lines on the 24 grid
        menu: "M3.5 6.8h17 M3.5 12h17 M3.5 17.2h17",
        // Window caption glyphs — 24×24 renderings of the Segoe Fluent
        // Icons caption geometry (E921 ChromeMinimize / E922 ChromeMaximize
        // / E923 ChromeRestore / E8BB ChromeClose). Native spec: the glyphs
        // fill a 10×10 box with 1px strokes → on this grid (10px display =
        // ×2.4 scale) that is full-bleed geometry with strokeWidth 2.4 —
        // pass `strokeWidth: 2.4` at the caption-button use site. Verified
        // against the Magpie CaptionButtonsControl spec (46×32 button,
        // 10×10 glyph viewbox, SymbolThemeFontFamily) and QWK's example
        // window-bar SVGs (minimize bar centered, restore = 12-grid
        // two-square overlap with the back square partially occluded).
        windowmin: "M1.2 12h21.6",
        windowmax: "M2.8 1.2h18.4a1.6 1.6 0 0 1 1.6 1.6v18.4a1.6 1.6 0 0 1-1.6 1.6H2.8a1.6 1.6 0 0 1-1.6-1.6V2.8a1.6 1.6 0 0 1 1.6-1.6z",
        windowrestore: "M1.2 5.2H18.8V22.8H1.2Z M5.2 5.2V1.2H22.8V18.8H20",
        windowclose: "M1.2 1.2L22.8 22.8 M22.8 1.2L1.2 22.8",
        // Animal avatars (CreateInstanceDialog picker data)
        cat: "M6.1 7.6 4.9 3.3l4.5 1.8 M17.9 7.6l1.2-4.3-4.5 1.8 M4.8 12.7a7.2 6.5 0 1 0 14.4 0a7.2 6.5 0 1 0-14.4 0 M9.5 12.4v.01 M14.5 12.4v.01",
        dog: "M5.2 13.2a6.8 6.1 0 1 0 13.6 0a6.8 6.1 0 1 0-13.6 0 M6.6 7.9C4.3 8.8 3.5 11.7 4.4 14.4c.4 1.2 1.9 1.2 2.4.1 M17.4 7.9c2.3.9 3.1 3.8 2.2 6.5-.4 1.2-1.9 1.2-2.4.1 M9.7 12.4v.01 M14.3 12.4v.01 M10.5 16.1a2.1 1.5 0 0 0 3 0",
        rabbit: "M7.4 5.9a1.5 3.1 0 1 0 3 0a1.5 3.1 0 1 0-3 0 M13.6 5.9a1.5 3.1 0 1 0 3 0a1.5 3.1 0 1 0-3 0 M5.7 15.3a6.3 5.5 0 1 0 12.6 0a6.3 5.5 0 1 0-12.6 0 M9.8 14.7v.01 M14.2 14.7v.01 M11.2 16.7h1.6",
        wolf: "M6 3.6 5 8.3 M18 3.6l1 4.7 M5 13.5a7 6.2 0 1 0 14 0a7 6.2 0 1 0-14 0 M9.6 12.7v.01 M14.4 12.7v.01 M10.5 17.1a2.4 1.8 0 1 0 4.8 0a2.4 1.8 0 1 0-4.8 0",
        fox: "M5.1 3.7l.7 5.2 M18.9 3.7l-.7 5.2 M5.2 13.6a6.8 6 0 1 0 13.6 0a6.8 6 0 1 0-13.6 0 M12 15.3v.8a1.6 1.6 0 0 1-3.1.4 M12 15.3v.8a1.6 1.6 0 0 0 3.1.4",
        panda: "M5.1 13a6.9 6.2 0 1 0 13.8 0a6.9 6.2 0 1 0-13.8 0 M5.9 7.6a2.1 2.1 0 1 0 .01 0 M18.1 7.6a2.1 2.1 0 1 0 .01 0 M8.5 12.4a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M12.5 12.4a1.5 2 0 1 0 3 0a1.5 2 0 1 0-3 0 M12 15.7v.01",
        bear: "M5.6 13.4a6.4 6 0 1 0 12.8 0a6.4 6 0 1 0-12.8 0 M6.4 7.8a2 2 0 1 0 .01 0 M17.6 7.8a2 2 0 1 0 .01 0 M9.8 12.6v.01 M14.2 12.6v.01 M9.7 15.3a2.3 1.7 0 1 0 4.6 0a2.3 1.7 0 1 0-4.6 0",
        tiger: "M5.3 13.4a6.7 6 0 1 0 13.4 0a6.7 6 0 1 0-13.4 0 M6.7 8.2a1.8 1.8 0 1 0 .01 0 M17.3 8.2a1.8 1.8 0 1 0 .01 0 M12 6.6v2.2 M4.7 12.5l2.2.7 M19.3 12.5l-2.2.7 M9.7 13.1v.01 M14.3 13.1v.01",
        lion: "M12.00 3.90 A1.55 1.55 0 0 1 16.76 5.45 A1.55 1.55 0 0 1 19.70 9.50 A1.55 1.55 0 0 1 19.70 14.50 A1.55 1.55 0 0 1 16.76 18.55 A1.55 1.55 0 0 1 12.00 20.10 A1.55 1.55 0 0 1 7.24 18.55 A1.55 1.55 0 0 1 4.30 14.50 A1.55 1.55 0 0 1 4.30 9.50 A1.55 1.55 0 0 1 7.24 5.45 A1.55 1.55 0 0 1 12.00 3.90Z M12 7.2a4.8 4.8 0 1 0 .01 0 M9.9 11.6v.01 M14.1 11.6v.01 M10.6 14.9a2 1.5 0 0 0 2.8 0",
        cow: "M5.4 13a6.6 6 0 1 0 13.2 0a6.6 6 0 1 0-13.2 0 M4.6 9.7a2 1.3 0 1 0 .01 0 M19.4 9.7a2 1.3 0 1 0 .01 0 M8.7 5.7C8 4.5 8.3 3.5 9.3 3.1 M15.3 5.7c.7-1.2.4-2.2-.6-2.6 M9.7 12v.01 M14.3 12v.01 M8.9 16.3a3.1 2.1 0 1 0 6.2 0a3.1 2.1 0 1 0-6.2 0 M10.7 16.2v.01 M13.3 16.2v.01",
        pig: "M5.3 13.1a6.7 5.9 0 1 0 13.4 0a6.7 5.9 0 1 0-13.4 0 M6.5 9.7 5.5 6.1l3.3 1.2 M17.5 9.7l1-3.6-3.3 1.2 M9.6 12v.01 M14.4 12v.01 M9.5 15.3a2.5 1.8 0 1 0 5 0a2.5 1.8 0 1 0-5 0 M11 15.2v.01 M13 15.2v.01",
        frog: "M5 14.4a7 4.7 0 1 0 14 0a7 4.7 0 1 0-14 0 M7.6 7.4a2.2 2.2 0 1 0 .01 0 M16.4 7.4a2.2 2.2 0 1 0 .01 0 M7.6 7.9v.01 M16.4 7.9v.01 M8.6 14.6c.9 1 2.1 1.6 3.4 1.6s2.5-.6 3.4-1.6",
        penguin: "M12 3.7c3.8 0 6.2 3.6 6.2 8.4 0 4.5-2.5 8.2-6.2 8.2s-6.2-3.7-6.2-8.2c0-4.8 2.4-8.4 6.2-8.4z M12 8.7a3.3 5 0 1 0 .01 0 M10.9 9.4h2.2l-1.1 1.4z",
        bird: "M5.8 12.8a6.2 6 0 1 0 12.4 0a6.2 6 0 1 0-12.4 0 M10.9 13.5h2.2L12 15.2z M9.6 11.4v.01 M14.4 11.4v.01 M10.7 6.9l-.9-2.4 2.2 1.1 M13.5 6.9l.9-2.4-2.2 1.1",
        chick: "M5.8 13.4a6.2 6 0 1 0 12.4 0a6.2 6 0 1 0-12.4 0 M12 6.9V4.9 M9.9 7.2 8.9 5.4 M14.1 7.2l1-1.8 M11 14h2l-1 1.5z M9.6 12.2v.01 M14.4 12.2v.01",
        owl: "M12 3.9c4 0 6.6 3 6.6 7.4 0 4.8-2.7 8.8-6.6 8.8s-6.6-4-6.6-8.8c0-4.4 2.6-7.4 6.6-7.4z M5.9 6.3 5.1 3.4l2.9 1.4 M18.1 6.3l.8-2.9-2.9 1.4 M9.3 10.7a2.2 2.2 0 1 0 .01 0 M14.7 10.7a2.2 2.2 0 1 0 .01 0 M9.3 10.9v.01 M14.7 10.9v.01 M11.3 13.6h1.4L12 15z",
        peach: "M5.4 13.3a6.6 6.5 0 1 0 13.2 0a6.6 6.5 0 1 0-13.2 0 M13.4 7.1c-1.1 1.9-1.2 10.5-.1 12.5 M12.9 6.7c.2-1.9 1.4-3.1 3.2-3.5-.2 1.9-1.4 3.1-3.2 3.5z"
    })

    // Legacy emoji → icon-key mapping. Covers the avatar picker vocabulary
    // plus every emoji the old QML hardcoded; unknown strings (custom data)
    // fall back to plain text below, so nothing is lost.
    readonly property var _emojiMap: ({
        "\u{1F431}": "cat", "\u{1F436}": "dog", "\u{1F430}": "rabbit",
        "\u{1F43A}": "wolf", "\u{1F98A}": "fox", "\u{1F43C}": "panda",
        "\u{1F43B}": "bear", "\u{1F42F}": "tiger", "\u{1F981}": "lion",
        "\u{1F42E}": "cow", "\u{1F437}": "pig", "\u{1F438}": "frog",
        "\u{1F427}": "penguin", "\u{1F426}": "bird", "\u{1F424}": "chick",
        "\u{1F989}": "owl", "\u{1F43E}": "paw", "\u{1F351}": "peach"
    })

    readonly property string _key: {
        if (name.length > 0) return name
        if (glyph.length > 0) return _emojiMap[glyph] ?? ""
        return ""
    }
    readonly property bool _known: _key.length > 0 && _paths[_key] !== undefined

    // Unknown/custom avatar data → plain text fallback (platform emoji
    // fonts render it when they can; never a blank box from us).
    Text {
        anchors.centerIn: parent
        visible: !root._known && root.glyph.length > 0
        text: root.glyph
        color: root.color
        font.pixelSize: Math.max(10, root.size * 0.8)
    }

    Shape {
        visible: root._known
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: root._known ? root._paths[root._key] : ""
            }
        }
    }
}
