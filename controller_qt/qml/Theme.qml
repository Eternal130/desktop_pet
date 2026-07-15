// Global theme singleton — runtime-switchable color palette.
//
// T26 (Phase 4.4): the single source of truth for every color the UI uses.
// Switching currentTheme re-evaluates every bound color binding immediately;
// no component restart, no page reload — QML's binding engine does the work.
//
// T28 will wire themeChanged → PanelStateManager.save so the active theme
// survives restarts; this file does NOT persist anything itself.
//
// Singleton (pragma Singleton) so any component can read Theme.bgColor / call
// Theme.setTheme(name) after `import DesktopPet` without threading a property
// through the tree. Registered under URI DesktopPet by qt_add_qml_module.
pragma Singleton

import QtQuick

QtObject {
    // ── Active theme name ────────────────────────────────────────────────────
    // Mutated only via setTheme() so the themeChanged signal always fires in
    // lockstep with the value flip. Default is the base Catppuccin Mocha theme.
    property string currentTheme: "深紫梦幻"

    // ── Catalog (presentation order) ─────────────────────────────────────────
    // Shown verbatim in the titlebar dropdown. MUST stay in sync with the keys
    // of _palettes below — both are hand-maintained by design so the dropdown
    // order is explicit rather than depending on Object.keys iteration order.
    readonly property var themes: ["深紫梦幻", "樱花浅粉", "赛博霓虹"]

    // Emitted whenever the active theme changes. Carries the new theme name so
    // listeners (T28: PanelStateManager) can persist it without re-reading state.
    signal themeChanged(string name)

    // ── Palette lookup ───────────────────────────────────────────────────────
    // One flat record per theme. _active is a readonly binding on currentTheme,
    // so every public color binding below re-evaluates the instant the name
    // flips — _active swaps to a different JS object reference, which fires the
    // property-change signal that bgColor/titleBarColor/... depend on.
    readonly property var _palettes: ({
        "深紫梦幻": {
            bg:         "#1e1e2e",   // Catppuccin Mocha "base"
            surface:    "#313244",   // Mocha "surface0"
            text:       "#cdd6f4",   // Mocha "text"
            accent:     "#cba6f7",   // Mocha "mauve"
            titleBar:   "#181825",   // Mocha "mantle" — darker than base for depth
            hover:      "#313244",   // Mocha "surface0"
            closeHover: "#f38ba8"    // Mocha "red"
        },
        "樱花浅粉": {
            bg:         "#fff0f5",   // lavender blush
            surface:    "#ffe4ec",   // misty rose
            text:       "#5c3d4e",   // deep plum — readable on pink
            accent:     "#ff69b4",   // hot pink
            titleBar:   "#ffe4ec",   // one shade deeper than bg
            hover:      "#ffd0e0",
            closeHover: "#ff5588"
        },
        "赛博霓虹": {
            bg:         "#0d0221",   // near-black violet
            surface:    "#1a0b2e",   // deep purple
            text:       "#00ffff",   // cyan
            accent:     "#ff00ff",   // magenta
            titleBar:   "#0a0118",   // darker than bg for depth
            hover:      "#2a0f4e",
            closeHover: "#ff0066"
        }
    })

    // Active palette object — atomically swaps when currentTheme changes.
    // Fallback to base theme guards against a transiently-empty currentTheme.
    readonly property var _active: _palettes[currentTheme] || _palettes["深紫梦幻"]

    // ── Public color bindings ────────────────────────────────────────────────
    // Core four (task contract) + three titlebar-specific (used by TitleBar).
    // All readonly+color so consumers get typed colors and instant re-render.
    readonly property color bgColor:         _active.bg
    readonly property color surfaceColor:    _active.surface
    readonly property color textColor:       _active.text
    readonly property color accentColor:     _active.accent
    readonly property color titleBarColor:   _active.titleBar
    readonly property color hoverColor:      _active.hover
    readonly property color closeHoverColor: _active.closeHover

    // ── Mutation ─────────────────────────────────────────────────────────────
    // Sets the active theme and notifies listeners. No-op for unknown names or
    // no-op transitions, so a bogus dropdown value cannot blank the palette.
    function setTheme(name) {
        if (name === currentTheme) return
        if (!(name in _palettes)) return
        currentTheme = name
        themeChanged(name)
    }
}
