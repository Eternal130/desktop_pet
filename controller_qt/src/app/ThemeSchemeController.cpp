#include "app/ThemeSchemeController.hpp"

#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QStyleHints>
#include <QSysInfo>

#include <spdlog/spdlog.h>

#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "logging/Logging.hpp"

namespace {

constexpr const char* kSystem = "system";
constexpr const char* kLight = "light";
constexpr const char* kDark = "dark";

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
