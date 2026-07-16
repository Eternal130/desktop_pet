#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/ConfigDir.hpp"
#include "core/EnvironmentChecker.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/WindowStateSaver.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Boot logging BEFORE any LOG_* call. ConfigDir::ensureDirectories creates
    // ~/.config/desktop-pet/{,instances,logs}/ so Logging::init can attach its
    // rotating file sink. A failure to create the tree is non-fatal (Logging
    // degrades to console-only — see Logging.hpp). A failed init() is likewise
    // non-fatal: it already installed the console sink + Qt message bridge.
    ConfigDir::ensureDirectories();
    Logging::init(ConfigDir::logsDir());

    // Load the persisted panel-level config (T28 window-state restore) so the
    // QML window can restore its initial position/size + theme on startup.
    // PanelStateManager handles: fresh install (writes defaults), legacy
    // panel-state.json migration, and corrupt-file fallback to defaults.
    PanelStateManager psm(ConfigDir::configDir());
    const PanelConfig panelCfg = psm.load();
    LOG_INFO("Restored panel config: panelX={} panelY={} {}x{} theme=\"{}\"",
             panelCfg.panelX, panelCfg.panelY,
             panelCfg.panelWidth, panelCfg.panelHeight,
             panelCfg.theme.toStdString());

    QQmlApplicationEngine engine;

    // EnvironmentChecker (T27) — exposed as a global QML context property
    // "envChecker" so every page can read the readiness probes without
    // per-page instantiation. Declared before `engine` and on the stack so it
    // outlives the QML engine (destroyed after engine during stack unwind).
    // WelcomePage.qml calls envChecker.runChecks() on Component.onCompleted.
    EnvironmentChecker envChecker;
    engine.rootContext()->setContextProperty("envChecker", &envChecker);

    // T28: pass the restored geometry + theme as individual QML context
    // properties so Main.qml can bind its x/y/width/height + call
    // Theme.setTheme(initialTheme) on startup. panelX/Y of -1 means "center on
    // screen" (PanelConfig.hpp); Main.qml derives the centered coordinates from
    // Screen.width/height when it sees the -1 sentinel.
    engine.rootContext()->setContextProperty("initialX", panelCfg.panelX);
    engine.rootContext()->setContextProperty("initialY", panelCfg.panelY);
    engine.rootContext()->setContextProperty("initialWidth", panelCfg.panelWidth);
    engine.rootContext()->setContextProperty("initialHeight", panelCfg.panelHeight);
    engine.rootContext()->setContextProperty("initialTheme", panelCfg.theme);

    // WindowStateSaver (T28) — Q_INVOKABLE saveWindowState called from
    // Main.qml's onClosing handler and from a Theme.themeChanged Connections
    // block. Declared before `engine` so it outlives the QML engine — the
    // onClosing handler fires during engine/window teardown and must reach a
    // live saver. Same stack-ordering discipline as EnvironmentChecker above.
    WindowStateSaver windowStateSaver(ConfigDir::configDir());
    engine.rootContext()->setContextProperty("windowStateSaver", &windowStateSaver);

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    const int exitCode = app.exec();
    // Flush the rotating file sink so the final log lines (incl. the close
    // handler's "Saved window state") reach disk before process tear-down.
    Logging::shutdown();
    return exitCode;
}
