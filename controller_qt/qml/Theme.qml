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
    // Modern minimal: light neutral canvas, near-flat surface, single accent.
    readonly property color bgColor:         "#fafafa"   // canvas
    readonly property color surfaceColor:    "#ffffff"   // cards / panels
    readonly property color textColor:       "#1a1a1a"   // primary text
    readonly property color accentColor:     "#5b5bd6"   // restrained indigo
    readonly property color titleBarColor:   "#ffffff"   // flat titlebar
    readonly property color hoverColor:      "#ececec"   // subtle hover fill
    readonly property color closeHoverColor: "#e5484d"   // destructive hover

    // ── Extended neutrals (surface hierarchy + borders) ─────────────────────
    readonly property color sidebarColor:    "#f4f4f5"   // sidebar rail
    readonly property color borderColor:     "#e4e4e7"   // hairline borders
    readonly property color mutedTextColor:  "#71717a"   // secondary text

    // ── Semantic status colors (universal good/warn/bad signals) ────────────
    readonly property color successColor:    "#30a46c"   // green
    readonly property color warningColor:    "#f5a623"   // amber
    readonly property color errorColor:      "#e5484d"   // red

    // ── Chart accent ramp (6 distinct, desaturated hues for sparklines) ────
    readonly property var chartColors: [
        "#5b5bd6",   // indigo
        "#0ea5e9",   // sky
        "#30a46c",   // green
        "#f59e0b",   // amber
        "#ec4899",   // pink
        "#14b8a6"    // teal
    ]

    // ── Mutation (kept for persistence compatibility) ───────────────────────
    function setTheme(name) {
        if (name === currentTheme) return
        if (themes.indexOf(name) === -1) return
        currentTheme = name
        themeChanged(name)
    }
}
