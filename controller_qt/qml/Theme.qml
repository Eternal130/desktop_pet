// Global design tokens — palette-migration revision (2026-10-13).
//
// COLOR SOURCE: the SYSTEM PALETTE ONLY. Every color below derives from a
// SystemPalette role (or Qt.alpha/lighter/darker of one), so the whole panel
// follows the platform color scheme — light/dark switches with the OS
// (Qt.styleHints.colorScheme), accent follows the style/theme default
// (FluentWinUI3 system accent on Windows, Fluent blue on Linux). There is NO
// in-app theme toggle and NO custom palette anymore; the old hand-rolled
// light/dark palettes, overlays and gradient stops were removed.
//
// Theme deliberately KEEPS its historical semantic token names (bgColor,
// textColor, accentColor, …) so the QML pages keep one indirection point:
// pages never hardcode palette roles, they read these tokens.
//
// ── COLOR LITERAL RULE ────────────────────────────────────────────────────
// NEVER write 8-digit hex colors ("#ffffff14") anywhere in qml/. QML parses
// them as #AARRGGBB — alpha FIRST — which is almost never what the author
// meant: it silently turned our dark-mode card borders into opaque yellow
// (11 sites, found 2026-10-13, all removed in this migration). Use
// Qt.rgba()/Qt.alpha() or a palette derivative instead. The only allowed hex
// literals are 6-digit (#RRGGBB) constants (semantic status colors below,
// chart ramp, fixed-dark log console).
pragma Singleton

import QtQuick

QtObject {
    // The one palette source. SystemPalette tracks QGuiApplication::palette,
    // which follows the platform theme and Qt.styleHints::colorScheme (also
    // flip-able for QA via PET_COLORSCHEME=dark|light in main.cpp).
    readonly property SystemPalette sys: SystemPalette { }

    // "Is the system scheme dark" — live-updates with the OS switch. Same
    // name as the pre-migration toggle so existing consumers keep working;
    // it is now DERIVED, not stored.
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.Dark

    // ── Core surfaces / text ──────────────────────────────────────────────
    readonly property color bgColor:        sys.window
    // card surface: light scheme = base (pure white over gray window);
    // dark scheme = window lifted ~6% (Fluent-style elevation — cards must
    // sit ABOVE the window; base in a dark palette goes the other way)
    readonly property color surfaceColor:   dark ? Qt.lighter(sys.window, 1.06)
                                                 : sys.base
    readonly property color textColor:      sys.windowText
    // secondary text: placeholder role is the platform-tuned muted tone
    readonly property color text2Color:     sys.placeholderText
    // tertiary/hints: alpha wash of the primary text (palette has no 3rd tier)
    readonly property color text3Color:     Qt.alpha(sys.windowText, 0.55)

    // ── Accent (style/system accent; FluentWinUI3 populates highlight) ───
    readonly property color accentColor:       sys.highlight
    readonly property color accentHoverColor:  Qt.darker(sys.highlight, 1.15)
    // readable text ON an accent fill (Fluent pairs highlight/highlightedText)
    readonly property color accentTextColor:   sys.highlightedText

    // ── Chrome ────────────────────────────────────────────────────────────
    // control borders (inputs, buttons, tiles): solid mid tone
    readonly property color borderColor:    sys.mid
    // hairline separators: faint wash of the text color, works on any scheme
    readonly property color hairlineColor:  Qt.alpha(sys.windowText, 0.08)
    // generic hover wash (Fluent-style text-alpha hover, scheme-agnostic)
    readonly property color hoverColor:     Qt.alpha(sys.windowText, 0.06)
    // neutral "off" surface behind icons/thumbnails
    readonly property color offBgColor:     Qt.alpha(sys.windowText, 0.05)

    // ── Semantic status ───────────────────────────────────────────────────
    // System palette has no success/warning roles — keep tuned constants with
    // per-scheme values (dark variants are the bright Fluent-2 dark tones so
    // text/borders keep contrast on dark surfaces; fixes the old same-value
    // dark readability problem).
    readonly property color successColor:    dark ? "#6ccb5f" : "#0f7b0f"
    readonly property color successBgColor:  Qt.alpha(successColor, dark ? 0.20 : 0.12)
    readonly property color warningColor:    dark ? "#ffb900" : "#9d5d00"
    readonly property color warningBgColor:  Qt.alpha(warningColor, dark ? 0.20 : 0.12)
    readonly property color errorColor:      dark ? "#ff99a4" : "#c42b1c"
    readonly property color errorBgColor:    Qt.alpha(errorColor, dark ? 0.20 : 0.12)

    // ── Chart ramp (KEPT: data-visualization palette, not UI theming) ─────
    readonly property var chartColors: [
        "#4cc2ff", "#f472d0", "#0f7b0f",
        "#eaa300", "#29b8db", "#5b5bd6"
    ]

    // ── Log console (KEPT: intentionally fixed dark terminal look) ────────
    readonly property color logBgColor: "#202020"
    readonly property color logTextColor: "#d6d6d6"

    // ── Radius (design mock: card 8 · button 5 · badge full) ─────────────
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

    // ── Overlay scrim (appended 2026-10-13, P2-14 token convergence) ──────
    // Modal-dialog scrim over the window: fixed dark wash on BOTH schemes
    // (a scrim must dim content regardless of light/dark UI) — same
    // "tuned constant" family as the log-console colors above. Replaces the
    // Qt.rgba(0,0,0,0.35) literals formerly hardcoded at the AppDialog and
    // CreateInstanceDialog scrim Rectangles.
    readonly property color scrimColor: Qt.rgba(0, 0, 0, 0.35)
}
