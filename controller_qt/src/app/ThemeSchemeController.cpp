#include "app/ThemeSchemeController.hpp"

#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QStyleHints>
#include <QSysInfo>

#include <spdlog/spdlog.h>

#include "core/DatabaseManager.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "logging/Logging.hpp"

namespace {

constexpr const char* kSystem = "system";
constexpr const char* kLight = "light";
constexpr const char* kDark = "dark";

// Dedicated panel kv row for the Mica opt-in switch. Kept OUT of the
// "panel" blob (PanelConfig) on purpose: it is a src/app-level product
// switch, not a config-model field, and a separate row survives every
// PanelStateManager save() untouched.
constexpr const char* kMicaEnabledKey = "mica_enabled";

// Normalize the stored string. Only exactly "true" enables — anything
// else (missing, legacy junk, "1", "TRUE") reads as the product default
// false, mirroring how normalize() treats the theme field.
inline bool parseMicaEnabled(const QString& value)
{
    return value == QLatin1String("true");
}

} // namespace

ThemeSchemeController::ThemeSchemeController(const QString& configDir,
                                             const QString& initialTheme,
                                             bool applyInitial,
                                             QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
    , m_theme(normalize(initialTheme))
    , m_originalPalette(QGuiApplication::palette())
{
    // Mica is opt-in: read the persisted switch (absent row → false) so
    // Main.qml's applyMaterial phases see the user's choice from the very
    // first pre-show call. Same per-call DatabaseManager connection
    // pattern PanelStateManager uses for the panel blob.
    if (loadMicaEnabled()) {
        LOG_INFO("ThemeSchemeController: mica_enabled={} (kv)", m_micaEnabled);
    } else {
        LOG_INFO("ThemeSchemeController: mica_enabled={} (kv unreadable, "
                 "default kept)", m_micaEnabled);
    }

    if (applyInitial) {
        applyToRuntime();
        LOG_INFO("ThemeSchemeController: initial theme \"{}\" applied",
                 m_theme.toStdString());
    } else {
        // PET_COLORSCHEME QA path: main.cpp already pinned the runtime
        // scheme; keep the stored value visible to the settings UI without
        // overriding the QA state.
        LOG_INFO("ThemeSchemeController: initial apply skipped "
                 "(PET_COLORSCHEME env active), stored theme \"{}\"",
                 m_theme.toStdString());
    }
}

QString ThemeSchemeController::dbPath() const
{
    // Same normalization PanelStateManager applies: the config dir arrives
    // with (or without) a trailing separator, the DB file sits directly
    // under it.
    QString root = m_configDir;
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + QStringLiteral("app.db");
}

bool ThemeSchemeController::loadMicaEnabled()
{
    DatabaseManager db;
    if (!db.open(dbPath())) {
        LOG_WARN("ThemeSchemeController: cannot open app.db for "
                 "mica_enabled read");
        return false;
    }
    m_micaEnabled = parseMicaEnabled(
        db.getValue(QLatin1String(kMicaEnabledKey), QStringLiteral("false")));
    return true;
}

bool ThemeSchemeController::persistMicaEnabled(bool enabled)
{
    DatabaseManager db;
    if (!db.open(dbPath())) {
        LOG_WARN("ThemeSchemeController: cannot open app.db for "
                 "mica_enabled write");
        return false;
    }
    if (!db.setValue(QLatin1String(kMicaEnabledKey),
                     enabled ? QStringLiteral("true")
                             : QStringLiteral("false"))) {
        LOG_WARN("ThemeSchemeController: mica_enabled kv write failed");
        return false;
    }
    return true;
}

void ThemeSchemeController::setMicaEnabled(bool enabled)
{
    if (enabled == m_micaEnabled) {
        return;
    }
    m_micaEnabled = enabled;

    // Write-through first, then notify — the same order as setTheme: QML
    // reacts to micaEnabledChanged by re-running applyMaterial, and a
    // failed persistence must still flip the live runtime (material state
    // is ephemeral by nature; only the NEXT launch reads the kv).
    if (persistMicaEnabled(enabled)) {
        LOG_INFO("ThemeSchemeController: mica_enabled set to {} (persisted)",
                 enabled);
    } else {
        LOG_WARN("ThemeSchemeController: mica_enabled set to {} but "
                 "panel kv save failed", enabled);
    }
    emit micaEnabledChanged(enabled);
}

QString ThemeSchemeController::normalize(const QString& theme)
{
    if (theme == QLatin1String(kLight) || theme == QLatin1String(kDark)) {
        return theme;
    }
    // Legacy defaults ("深紫梦幻") and anything unknown read as follow-system.
    return QLatin1String(kSystem);
}

void ThemeSchemeController::setTheme(const QString& theme)
{
    const QString norm = normalize(theme);
    if (norm == m_theme) {
        return;
    }
    m_theme = norm;
    applyToRuntime();

    // Persist ONLY the theme field — the same load-modify-save cycle
    // WindowStateSaver uses, so every other PanelConfig field survives and
    // the write stays atomic (QSaveFile inside PanelStateManager).
    PanelStateManager psm(m_configDir);
    PanelConfig cfg = psm.load();
    cfg.theme = m_theme;
    if (!psm.save(cfg)) {
        // Runtime switch still happened; only the persistence failed.
        LOG_WARN("ThemeSchemeController: theme \"{}\" applied but "
                 "panel kv save failed", m_theme.toStdString());
    } else {
        LOG_INFO("ThemeSchemeController: theme set to \"{}\" (persisted)",
                 m_theme.toStdString());
    }
    emit themeChanged(m_theme);
}

void ThemeSchemeController::applyToRuntime()
{
    auto* hints = QGuiApplication::styleHints();
    if (m_theme == QLatin1String(kDark)) {
        hints->setColorScheme(Qt::ColorScheme::Dark);
        QGuiApplication::setPalette(makeDarkPalette());
    } else if (m_theme == QLatin1String(kLight)) {
        hints->setColorScheme(Qt::ColorScheme::Light);
        QGuiApplication::setPalette(makeLightPalette());
    } else {
        // Unknown = clear the app-level override, follow the system again.
        hints->setColorScheme(Qt::ColorScheme::Unknown);
        QGuiApplication::setPalette(m_originalPalette);
    }
}

QPalette ThemeSchemeController::makeDarkPalette()
{
    // Fusion-dark-like values, verbatim from the PET_COLORSCHEME QA table in
    // main.cpp (visually identical to the QA dark screenshots).
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(0x2b2b2b));
    dark.setColor(QPalette::WindowText, QColor(0xffffff));
    dark.setColor(QPalette::Base, QColor(0x1f1f1f));
    dark.setColor(QPalette::AlternateBase, QColor(0x2a2a2a));
    dark.setColor(QPalette::Text, QColor(0xdcdcdc));
    dark.setColor(QPalette::PlaceholderText, QColor(0xa0a0a0));
    dark.setColor(QPalette::Button, QColor(0x333333));
    dark.setColor(QPalette::ButtonText, QColor(0xffffff));
    dark.setColor(QPalette::Highlight, QColor(0x4cc2ff));
    dark.setColor(QPalette::HighlightedText, QColor(0x0b0b0b));
    dark.setColor(QPalette::ToolTipBase, QColor(0x3a3a3a));
    dark.setColor(QPalette::ToolTipText, QColor(0xffffff));
    dark.setColor(QPalette::Mid, QColor(0x555555));
    dark.setColor(QPalette::Link, QColor(0x4cc2ff));
    return dark;
}

QPalette ThemeSchemeController::makeLightPalette()
{
    // Light counterpart of the QA table (Fluent-light surfaces + accent).
    QPalette light;
    light.setColor(QPalette::Window, QColor(0xf3f3f3));
    light.setColor(QPalette::WindowText, QColor(0x1b1b1b));
    light.setColor(QPalette::Base, QColor(0xffffff));
    light.setColor(QPalette::AlternateBase, QColor(0xf9f9f9));
    light.setColor(QPalette::Text, QColor(0x1b1b1b));
    light.setColor(QPalette::PlaceholderText, QColor(0x8a8a8a));
    light.setColor(QPalette::Button, QColor(0xfbfbfb));
    light.setColor(QPalette::ButtonText, QColor(0x1b1b1b));
    light.setColor(QPalette::Highlight, QColor(0x0067c0));
    light.setColor(QPalette::HighlightedText, QColor(0xffffff));
    light.setColor(QPalette::ToolTipBase, QColor(0xffffff));
    light.setColor(QPalette::ToolTipText, QColor(0x1b1b1b));
    light.setColor(QPalette::Mid, QColor(0xc8c8c8));
    light.setColor(QPalette::Link, QColor(0x0067c0));
    return light;
}

QString ThemeSchemeController::osVersion() const
{
#if defined(Q_OS_WIN)
    // major.minor.build — build ≥ 22621 is Win11 22H2, the DWM
    // DWMWA_SYSTEMBACKDROP_TYPE (Mica) gate inside QWK.
    const auto& os = QOperatingSystemVersion::current();
    return QStringLiteral("Windows %1.%2 build %3")
        .arg(os.majorVersion())
        .arg(os.minorVersion())
        .arg(os.microVersion());
#else
    return QStringLiteral("%1 %2 (no window material)")
        .arg(QSysInfo::productType(), QSysInfo::productVersion());
#endif
}
