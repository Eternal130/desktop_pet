// Global theme singleton — Fluent UI redesign palette (docs/design/
// fluent-ui-redesign.html) with light/dark modes and a mutable accent.
//
// Light values mirror the design mock :root block verbatim. Dark values are
// the Fluent 2 dark equivalents. accentColor is writable (SettingsPage §⑤
// swatches); all dependent colors recompute via bindings.
pragma Singleton

import QtQuick

QtObject {
    // Persisted theme name (WindowStateSaver round-trips this string).
    property string currentTheme: "浅色"

    // "light" | "dark" — derived from currentTheme so T28 persistence keeps
    // working; setDarkMode() is the mutation entry point.
    readonly property bool dark: currentTheme === "深色"
    function setDarkMode(d) {
        setTheme(d ? "深色" : "浅色")
    }

    readonly property var themes: ["浅色", "深色"]

    signal themeChanged(string name)

    // ── Core palette ──────────────────────────────────────────────────────
    readonly property color bgColor:       dark ? "#202020" : "#f3f3f3"
    // Design-mock body gradient (135deg, near-instant hues over the canvas)
    readonly property color bgGradA: dark ? "#1d1d21" : "#eef0f7"
    readonly property color bgGradB: dark ? "#202024" : "#f3f0f7"
    readonly property color bgGradC: dark ? "#1c1f1e" : "#f0f4f2"
    // Titlebar / nav are TRANSLUCENT layers over that canvas (mock: .55/.75)
    readonly property color titleBarOverlay: dark ? Qt.rgba(0.17, 0.17, 0.18, 0.55)
                                                  : Qt.rgba(1, 1, 1, 0.55)
    readonly property color navOverlay:     dark ? Qt.rgba(0.14, 0.14, 0.15, 0.75)
                                                  : Qt.rgba(0.95, 0.95, 0.95, 0.75)
    readonly property color hairlineColor:  dark ? "#ffffff12" : "#0000000d"
    readonly property color surfaceColor:  dark ? "#2b2b2b" : "#ffffff"
    readonly property color navColor:      dark ? "#272727" : "#f9f9f9"
    readonly property color titleBarColor: dark ? "#2b2b2b" : "#ffffff"
    readonly property color textColor:     dark ? "#ffffff" : "#1a1a1a"
    readonly property color text2Color:    dark ? "#c8c8c8" : "#5c5c5c"
    readonly property color text3Color:    dark ? "#9a9a9a" : "#8a8a8a"
    property color accentColor: "#5b5bd6"   // design mock --accent
    readonly property color accentHoverColor: "#4c4cc4"
    readonly property color accentLightColor:
        dark ? Qt.rgba(0.36, 0.36, 0.84, 0.22) : "#e8e8fb"
    readonly property color hoverColor:     dark ? "#333333" : "#e9e9e9"
    readonly property color closeHoverColor: "#c42b1c"

    // kept aliases (older bindings used these names)
    readonly property color mutedTextColor: text2Color
    readonly property color borderColor:    dark ? "#3d3d3d" : "#e5e5e5"
    readonly property color sidebarColor:   navColor

    // ── Semantic status (design mock) ────────────────────────────────────
    readonly property color successColor: "#0f7b0f"
    readonly property color successBgColor: dark ? "#11331122" : "#e6f2e6"
    readonly property color warningColor: "#9d5d00"
    readonly property color warningBgColor: dark ? "#9d5d0022" : "#fdf3dd"
    readonly property color errorColor: "#c42b1c"
    readonly property color errorBgColor: dark ? "#c42b1c22" : "#fde7e7"
    readonly property color offBgColor: dark ? "#ffffff14" : "#0000000f"

    // ── Chart ramp (design mock chart1..chart6) ──────────────────────────
    readonly property var chartColors: [
        "#4cc2ff", "#f472d0", "#0f7b0f",
        "#eaa300", "#29b8db", "#5b5bd6"
    ]

    // ── Elevation (floating layers only; resting cards use a hairline) ──
    readonly property color shadowColor: Qt.rgba(0, 0, 0, dark ? 0.5 : 0.22)

    // ── Radius (design mock: card 8 · button 5 · badge full) ────────────
    readonly property real radiusSm: 4
    readonly property real radiusMd: 5
    readonly property real radiusLg: 8

    // ── Spacing (8pt grid: page 32 / card 20 / group 14 / item 8) ───────
    readonly property real spacePage: 32
    readonly property real spaceCard: 20
    readonly property real spaceGroup: 14
    readonly property real spaceItem: 8

    // ── Typography ────────────────────────────────────────────────────────
    readonly property int fontCaption: 11
    readonly property int fontBody: 13
    readonly property int fontSubhead: 15
    readonly property int fontTitle: 24

    // Log console (design mock .log)
    readonly property color logBgColor: "#202020"
    readonly property color logTextColor: "#d6d6d6"

    function accentAlpha(a) {
        return Qt.rgba(accentColor.r, accentColor.g, accentColor.b, a)
    }
    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    function setTheme(name) {
        if (name === currentTheme) return
        if (themes.indexOf(name) === -1) return
        currentTheme = name
        themeChanged(name)
    }
}
