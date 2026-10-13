// Composition root (P3 final). Everything else lives in src/app/:
// ScreenshotRunner (M1, visual-QA modes), PanelApplication (M2, service
// tree), PanelUiBoot (M3, UI bridges + the 17 context properties),
// AppFontGuard (M4, CJK tofu guard). main() is sequencing only.
#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QCoreApplication>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

#include <chrono>

#include "app/AppFontGuard.hpp"
#include "app/PanelApplication.hpp"
#include "app/PanelUiBoot.hpp"
#include "app/ScreenshotRunner.hpp"
#include "core/ConfigDir.hpp"
#include "core/PluginHost.hpp"
#include "core/StaticPluginBootstrap.hpp"
#include "logging/Logging.hpp"

int main(int argc, char* argv[])
{
    // Quick Controls style: FluentWinUI3 (Qt 6.8+), selected through the embedded
    // qtquickcontrols2.conf ([Controls] Style=FluentWinUI3, attached to this
    // exe's resources at the root prefix in CMakeLists.txt). Do NOT set
    // QT_QUICK_CONTROLS_STYLE unconditionally here — the environment variable
    // would override the conf file.
    // History: this used to qputenv "Basic" because the default native (Windows)
    // style forbids customizing Control background/contentItem and crashed
    // QtCharts' ChartView under a ComboBox with a custom background. FluentWinUI3
    // is a fully QML-implemented style that ALLOWS those customizations, so the
    // app's Theme-bound overrides (AppComboBox/AppScrollBar + page popups) keep
    // working.
    //
    // FluentWinUI3 availability guard: a Qt build without the FluentWinUI3 QML
    // style module (Ubuntu/Debian archives omit it; a trimmed windeployqt
    // output could too) does NOT gracefully fall back — Main.qml fails outright
    // with "module QtQuick.Controls.FluentWinUI3 is not installed" (white
    // screen; verified on the system Qt 6.10). Probe the QML imports dir
    // before the application object exists and, on a miss, pin Basic through
    // the env var (which takes precedence over the embedded conf). An env
    // value pointing at some other valid style is respected as-is.
    {
        const QString fluentModuleDir =
            QLibraryInfo::path(QLibraryInfo::QmlImportsPath) +
            QStringLiteral("/QtQuick/Controls/FluentWinUI3");
        if (!QDir(fluentModuleDir).exists()) {
            const QByteArray requested = qgetenv("QT_QUICK_CONTROLS_STYLE");
            if (requested.isEmpty() || requested.compare(
                    QByteArrayLiteral("FluentWinUI3"),
                    Qt::CaseInsensitive) == 0) {
                qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
            }
        }
    }

    // Hidden exit-QA hook (B.2 v2), deliberately absent from any help text:
    // --self-quit <ms> runs the FULL normal startup (engine load, window
    // creation, exec()) and then quits via QCoreApplication::quit() after
    // <ms> — exercising the real teardown destructor chain, NOT the QA
    // modes' std::exit path. Exit code 0 is the M4 gate.
    int selfQuitMs = -1;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromUtf8(argv[i]);
        if (arg == QStringLiteral("--self-quit") && i + 1 < argc)
            selfQuitMs = QString::fromUtf8(argv[++i]).toInt();
    }

    // ── Storage-layout identity (QStandardPaths) ──────────────────────────
    // App name "desktop-pet" + deliberately NO organization name keeps every
    // QStandardPaths location flat (<root>/desktop-pet). MUST be set before
    // anything queries ConfigDir:: (it is static state, so even pre-QApplication
    // is fine; an explicitly-set name is never reset by the app object).
    QCoreApplication::setApplicationName(QStringLiteral("desktop-pet"));

    // ── T24 cold-start timing anchor ─────────────────────────────────────
    // Captured BEFORE QGuiApplication construction so Qt framework init cost
    // is included in the cold-start delta; std::chrono is provably safe
    // pre-QCoreApplication. Registered as the coldStartT0Ms context property
    // by PanelUiBoot; Main.qml's Component.onCompleted logs the visible
    // delta ("COLD_START_MS=<n>").
    const auto t0SinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const qint64 coldStartT0Ms = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t0SinceEpoch).count());

    // QApplication (NOT QGuiApplication): Qt 6.10 QtCharts' ChartView depends
    // on QtWidgets initialization — under a bare QGuiApplication the ChartView
    // creation crashes with 0xC0000005 inside Qt6Widgets.dll (MonitorPage).
    // QtWidgets is already linked for QSystemTrayIcon (T13), so this costs
    // nothing extra.
    QApplication app(argc, argv);

    // QA/dev seam: PET_COLORSCHEME=dark|light overrides the system color
    // scheme at runtime (QStyleHints::setColorScheme, Qt 6.8+). The palette-
    // driven Theme (SystemPalette + Qt.styleHints.colorScheme) and the
    // FluentWinUI3 style both follow this, so screenshot QA can capture the
    // dark variant deterministically on any desktop. Absent env = native
    // behavior; there is NO in-app theme toggle anymore.
    //
    // Linux xcb caveat: the generic platform theme ignores colorScheme when
    // building the DEFAULT application palette (only theme plugins like
    // xdgdesktopportal/gtk3 honor it). Switching the (unused-for-drawing)
    // widget style to Fusion makes its standardPalette follow the scheme, so
    // QGuiApplication::palette — and with it SystemPalette — flips too.
    if (const QByteArray scheme = qgetenv("PET_COLORSCHEME");
        scheme == QByteArrayLiteral("dark") ||
        scheme == QByteArrayLiteral("light")) {
        const bool wantDark = scheme == QByteArrayLiteral("dark");
        QGuiApplication::styleHints()->setColorScheme(
            wantDark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        if (wantDark) {
            // xcb-generic never hands Qt a dark default palette (only real
            // theme plugins do), so simulate what a dark desktop theme would
            // deliver: an explicit dark QPalette. Fusion-dark-like values.
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
            QGuiApplication::setPalette(dark);
        }
    }

    // Boot logging BEFORE any LOG_* call (both calls non-fatal on failure —
    // console-only degradation, see Logging.hpp). Migration runs BEFORE
    // ensureDirectories(): the scaffold (instances/ + logs/) would otherwise
    // make a fresh config dir look "already populated" and skip migration.
    // No-op returning false on Linux (path unchanged there).
    ConfigDir::migrateLegacyIfNeeded();
    ConfigDir::ensureDirectories();
    Logging::init(ConfigDir::logsDir());

    // CJK tofu guard — after Logging::init, before engine load (see
    // app/AppFontGuard.cpp for the full rationale; WARN-only on failure).
    installDefaultFontWithCjk();

    // Service tree (M2) + legacy instance-0 sender holders (M4): five
    // QObjects + salvo/PM in one parent tree; destruction-order annotations
    // in PanelApplication.cpp.
    PanelApplication panelApp;
    panelApp.wireLegacyInstanceZeroSenders();

    // P4 (§B.6 two-phase loading): push the CMake-generated factory table
    // (static_plugins.cpp, exe-linked) into the registry — after the
    // service tree exists, before the UI boot mounts the page model.
    pet_register_static_plugins(panelApp.pluginRegistry());

    QQmlApplicationEngine engine;

    // All 17 context properties + UI-bridge wiring (names & order verbatim,
    // QML unchanged); PanelUiBoot is parented to the engine — mount-point
    // destruction contract in PanelUiBoot.cpp. Also logs
    // COLD_START_PRE_LOAD_MS as its last statement.
    PanelUiBoot* const uiBoot =
        PanelUiBoot::registerAll(engine, panelApp, coldStartT0Ms);

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    // Auto-start salvo — singleShot(0) so the first frame paints before the
    // (blocking, process-spawning) start() calls run (PanelApplication).
    panelApp.launchAutoStartInstances();

    // P4 (§B.3 lifecycle): plugin initialize() AFTER the UI boot mounted
    // the page model / manager (PluginHost::initializeAll registers pages
    // reactively; failures are isolated per §A.1 and surface in the
    // Settings plugin section). Synchronous so --screenshot runs capture
    // settled plugin pages.
    panelApp.pluginHost()->initializeAll();

    // ── Visual-QA modes (--screenshot / --bubble) ─────────────────────────
    // Extracted to app/ScreenshotRunner (M1). Returns only when no QA flag
    // was passed; an active QA mode ends the process via std::exit inside
    // (teardown-history notes live in ScreenshotRunner.cpp).
    runScreenshotQa(app, engine, *uiBoot->notificationStream(), argc, argv);

    // Schedule the exit-QA quit AFTER the QA-mode call: screenshot/bubble
    // modes terminate the process themselves and take precedence.
    if (selfQuitMs >= 0) {
        QTimer::singleShot(selfQuitMs, &app, &QCoreApplication::quit);
    }

    const int exitCode = app.exec();
    // P4 (§B.3): reverse-order plugin shutdown on the normal exit path —
    // before Logging::shutdown() so the shutdown log lines land in the
    // sink; the host is in the PanelApplication tree (still alive here).
    // QA modes that std::exit skip this deliberately (documented).
    panelApp.pluginHost()->shutdownAll();
    Logging::shutdown();
    return exitCode;
}
