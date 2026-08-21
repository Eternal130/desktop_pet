// Global theme singleton — modern minimal design system.
//
// Single theme (the legacy 深紫梦幻 / 樱花浅粉 / 赛博霓虹 catalog was removed).
// The public API surface (bgColor / surfaceColor / textColor / accentColor /
// titleBarColor / hoverColor / closeHoverColor + currentTheme / themes /
// setTheme) is preserved so existing bindings and the T28 persistence path
// (WindowStateSaver stores Theme.currentTheme) keep working unchanged.
//
// Palette: modern minimal — near-white canvas, soft neutral surface, one
// restrained indigo accent, hairline borders, semantic status colors kept
// desaturated for a calm dashboard feel.
//
// Singleton (pragma Singleton) so any component can read Theme.bgColor after
// `import DesktopPet` without threading a property through the tree.
// Registered under URI DesktopPet by qt_add_qml_module.
pragma Singleton

import QtQuick

QtObject {
    // ── Active theme name ────────────────────────────────────────────────────
    // Kept for T28 persistence compatibility: WindowStateSaver round-trips
    // this string through panel.json. setTheme() no-ops on any other name.
    property string currentTheme: "现代简约"

    // Catalog of one — Main.qml restores the persisted name; unknown stored
    // values fall back harmlessly to this entry.
    readonly property var themes: ["现代简约"]

    // Emitted on theme change. Never fires in practice (single theme), but
    // Main.qml's Connections keeps its listener — zero behavioral risk.
    signal themeChanged(string name)

    // ── Core palette ──────────────────────────────────────────────────────────
    // Fully Fluent: Fluent 2 system blue accent, neutral cool-gray surfaces.
    readonly property color bgColor:         "#f3f3f3"   // canvas (Fluent layer)
    readonly property color surfaceColor:    "#ffffff"   // cards / panels
    readonly property color textColor:       "#1a1a1a"   // primary text
    readonly property color accentColor:     "#005fb8"   // Fluent 2 brand blue
    readonly property color titleBarColor:   "#ffffff"   // flat titlebar
    readonly property color hoverColor:      "#e5e5e5"   // neutral hover fill
    readonly property color closeHoverColor: "#c42b1c"   // Fluent error red

    // ── Extended neutrals (surface hierarchy + borders) ─────────────────────
    readonly property color sidebarColor:    "#f4f4f5"   // sidebar rail
    readonly property color borderColor:     "#e4e4e7"   // hairline borders
    readonly property color mutedTextColor:  "#71717a"   // secondary text

    // ── Semantic status colors (Fluent 2 palette) ──────────────────────────
    readonly property color successColor:    "#0e700e"   // fluent green
    readonly property color warningColor:    "#9d5d00"   // fluent amber
    readonly property color errorColor:      "#c42b1c"   // fluent red

    // ── Chart accent ramp (Fluent-hued, 6 distinct for sparklines) ──────────
    readonly property var chartColors: [
        "#005fb8",   // fluent blue
        "#0f6cbd",   // blue shade
        "#0e700e",   // fluent green
        "#9d5d00",   // fluent amber
        "#c239b3",   // fluent magenta
        "#038387"    // fluent teal
    ]

    // ── Elevation tokens — Linear/Notion consensus: RESTING cards get NO
    // shadow (1px border is the separator); shadows only for floating layers
    // (dialogs, popovers). Values = Fluent 2 shadow8 two-layer recipe.
    readonly property color  shadowColor:   Qt.rgba(0, 0, 0, 0.14)
    readonly property real   shadowBlur:    8
    readonly property real   shadowOffsetY: 4

    // ── Radius scale (4px grid; consensus: controls 4-6, cards 8-12) ───────
    readonly property real radiusSm: 4    // badges, chips, small controls
    readonly property real radiusMd: 6    // buttons, inputs
    readonly property real radiusLg: 12   // cards, panels

    // ── Typography scale (weight hierarchy: 600 heads / 500 sub / 400 body)─
    readonly property int fontCaption: 11
    readonly property int fontBody:     13
    readonly property int fontSubhead:  15
    readonly property int fontTitle:    18

    // ── Mutation (kept for persistence compatibility) ───────────────────────
    function setTheme(name) {
        if (name === currentTheme) return
        if (themes.indexOf(name) === -1) return
        currentTheme = name
        themeChanged(name)
    }
}
