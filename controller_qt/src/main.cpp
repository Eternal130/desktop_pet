// Composition root (P3 final). Everything else lives in src/app/:
// ScreenshotRunner (M1, visual-QA modes), PanelApplication (M2, service
// tree), PanelUiBoot (M3, UI bridges + the 17 context properties),
// AppFontGuard (M4, CJK tofu guard). main() is sequencing only.
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QCoreApplication>
#include <QTimer>

#include <chrono>

#include "app/AppFontGuard.hpp"
#include "app/PanelApplication.hpp"
#include "app/PanelUiBoot.hpp"
#include "app/ScreenshotRunner.hpp"
#include "core/ConfigDir.hpp"
#include "logging/Logging.hpp"

int main(int argc, char* argv[])
{
    // Use the Basic QuickControls style: the default native (Windows) style
    // forbids customizing Control background/contentItem, which (a) breaks
    // Theme-bound CheckBox/ComboBox colors and (b) crashes the renderer when
    // QtCharts' ChartView loads under a ComboBox with a custom background.
    // Basic imposes no palette of its own, so our Theme singleton owns all
    // visuals. Must be set before QGuiApplication construction.
    // Ref: https://doc.qt.io/qt-6/qtquickcontrols2-styles.html
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

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

    // Boot logging BEFORE any LOG_* call (both calls non-fatal on failure —
    // console-only degradation, see Logging.hpp).
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
    Logging::shutdown();
    return exitCode;
}
