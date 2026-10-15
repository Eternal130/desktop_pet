#pragma once

#include <QObject>
#include <QPalette>
#include <QString>

// ThemeSchemeController (2026-10-15) — the panel's runtime light/dark/
// follow-system switcher, productized from the PET_COLORSCHEME QA seam.
//
// Product decision revision: the 2026-10-13 palette migration made the panel
// strictly follow the system color scheme; Windows users asked for an
// independent in-panel switch back. The Theme.qml token layer is untouched
// (still SystemPalette-driven) — this controller only decides WHICH palette
// and color scheme the system presents:
//   "dark"   → QStyleHints::setColorScheme(Dark)  + explicit dark QPalette
//   "light"  → QStyleHints::setColorScheme(Light) + explicit light QPalette
//   "system" → setColorScheme(Unknown)            + restore the palette the
//              app was launched with (native theme/accent returns).
// The explicit palettes are required because Linux xcb-generic ignores the
// colorScheme hint when building the default palette (the reason the QA env
// overrides it); using them on Windows too keeps the forced modes pixel-
// deterministic. 跟随系统 restores the launch-time palette so the native
// accent color comes back.
//
// Layering / boundary: lives in src/app (composition-root layer) — it may
// touch core persistence directly (same privilege as PanelUiBoot). The
// QML Settings page writes through the "themeScheme" context property
// (Q_INVOKABLE setTheme), never through core. Persistence reuses the
// PanelStateManager load-modify-save cycle on the SAME panel.theme kv field
// WindowStateSaver already writes — any other PanelConfig field survives
// unchanged. Values are normalized: anything that is not exactly "light"/
// "dark" (including PanelConfig's legacy default "深紫梦幻") reads and
// persists as "system", which is also the default mode.
//
// QA interplay: when PET_COLORSCHEME=dark|light is set, main.cpp already
// applied a deterministic scheme before this controller exists — PanelUiBoot
// passes applyInitial=false in that case so QA screenshots stay exact.
//
// Mica switch (2026-10-15, product revision): Windows 11 build 26200
// reports QWK setWindowAttribute("mica") success in both applyMaterial
// phases yet the backdrop never renders visually (Qt Quick + newer Win11
// DWM composition; no upstream fix) — micaActive=true then strips the
// window base color for nothing, which reads as a white shell and collapses
// dark-mode contrast. The attribute return value is therefore NOT trusted
// as visual evidence: the material is now opt-in via the micaEnabled
// property (default false). Main.qml's applyMaterial gates micaActive on
// this flag; the toggle lives in 设置 → 外观. Persistence is a dedicated
// panel kv row "mica_enabled" ("true"/"false") written through
// DatabaseManager::setValue directly — same panel-kv privilege this class
// already exercises via PanelStateManager for the "theme" field, and
// deliberately NOT a PanelConfig field / ISettingsApi surface (no SDK
// change; the plugin-facing settings API is untouched).
class ThemeSchemeController : public QObject
{
    Q_OBJECT
    // Opt-in Mica material (default false — see class comment). Read by
    // Main.qml's applyMaterial on every phase; toggled live from the
    // settings page, persisted immediately.
    Q_PROPERTY(bool micaEnabled READ micaEnabled WRITE setMicaEnabled
               NOTIFY micaEnabledChanged)

public:
    // configDir: panel kv root (ConfigDir::configDir() in production).
    // initialTheme: PanelConfig.theme as loaded (may be legacy junk).
    // applyInitial: false when the PET_COLORSCHEME QA env already applied a
    // scheme in main.cpp.
    explicit ThemeSchemeController(const QString& configDir,
                                   const QString& initialTheme,
                                   bool applyInitial,
                                   QObject* parent = nullptr);

    // Normalized current mode: "system" | "light" | "dark".
    Q_INVOKABLE QString theme() const { return m_theme; }

    Q_INVOKABLE void setTheme(const QString& theme);

    bool micaEnabled() const { return m_micaEnabled; }
    void setMicaEnabled(bool enabled);

    // "Windows 11 (build 22621)"-style diagnostic string for the
    // MATERIAL_APPLIED startup log — build ≥ 22621 is what gates the
    // DWMWA_SYSTEMBACKDROP_TYPE (Mica) API inside QWK.
    Q_INVOKABLE QString osVersion() const;

signals:
    void themeChanged(const QString& theme);
    void micaEnabledChanged(bool enabled);

private:
    void applyToRuntime();
    static QPalette makeDarkPalette();
    static QPalette makeLightPalette();
    static QString normalize(const QString& theme);
    QString dbPath() const;
    bool loadMicaEnabled();
    bool persistMicaEnabled(bool enabled);

    QString m_configDir;
    QString m_theme;          // normalized: "system" | "light" | "dark"
    QPalette m_originalPalette;
    bool m_micaEnabled = false;  // product default OFF — see class comment
};
