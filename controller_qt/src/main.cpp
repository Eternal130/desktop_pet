#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/QtQml>
#include <QJsonDocument>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/ConfigDir.hpp"
#include "core/EnvironmentChecker.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelConfigController.hpp"
#include "core/PanelStateManager.hpp"
#include "core/WindowStateSaver.hpp"
#include "core/StartupSalvo.hpp"
#include "core/ProcessManager.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"
#include "network/PendingRequests.hpp"
#include "system/AutoLaunchManager.hpp"
#include "system/TrayManager.hpp"

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

    // ── Network stack (Phase 5, todo 1) ───────────────────────────────────
    // Declared BEFORE QQmlApplicationEngine so they outlive it during stack
    // unwind (reverse-destruction order: engine destroyed first, then these).
    //
    // WsServer: the WS server socket (renderer connects here on 127.0.0.1:9001).
    // PendingRequests: GLOBAL id→result table (M2 resolution — instance-agnostic;
    // response id matching must work across all instances, so this is the ONLY
    // network object shared globally; each InstanceSession owns its own
    // MessageDispatcher + EventRegistry).
    // StartupSalvo + ProcessManager: Phase-5 single-instance holders wired here
    // for sender injection; todo 2 (InstanceSession) owns them per-instance.
    // These globals are now redundant (InstanceSession owns its own), but they
    // are harmless and NetworkWiringTest asserts the sender wiring, so they
    // stay until a dedicated cleanup task.
    WsServer wsServer;
    PendingRequests pendingRequests;
    StartupSalvo startupSalvo;
    ProcessManager processManager;

    // Wire sender-injection seams → WsServer::sendText(instanceId, ...). These
    // legacy globals always targeted instance_id 0 (Phase 0-4 single-instance
    // assumption); InstanceSession owns its own per-instance senders below.
    startupSalvo.setCommandSender([&wsServer](const QString& json) {
        wsServer.sendText(0, json);
    });
    processManager.setShutdownSender([&wsServer]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        wsServer.sendText(0, QString::fromUtf8(json));
    });

    // ── Instance roster (Phase 5, todo 7) ────────────────────────────────
    // InstanceManager owns every InstanceSession, loads existing instances
    // from panel.json instanceIds order on construction (m4 fix), and persists
    // roster mutations via the injected savePanel callback (m5 fix — wired to
    // PanelStateManager::save so create/delete reach panel.json immediately).
    //
    // Stack-ordering: declared AFTER psm (captures &psm) and AFTER wsServer/
    // pendingRequests (captured by reference), but BEFORE the engine so it
    // outlives the QML context. parent=nullptr — InstanceManager owns its
    // InstanceSessions explicitly via qDeleteAll (InstanceManager.hpp), so a
    // Qt parent on the manager itself would risk double-delete during stack
    // unwind. The Session objects inside use parent=nullptr too (same reason).
    InstanceManager instanceManager(
        ConfigDir::configDir(), wsServer, pendingRequests,
        [&psm](const PanelConfig& cfg) { psm.save(cfg); },
        nullptr);

    // WsServer::messageReceived(int instanceId, env) → InstanceManager::route.
    // Phase 5 todo 11: per-instanceId demux — the WsServer carries the parsed
    // ?instance_id=N on the signal so route() forwards to the right session
    // directly (no Phase-5 single-instance row-0 hack). InstanceManager::route
    // does a linear scan by instanceId() (sidebar-sized N, O(N) is fine).
    QObject::connect(&wsServer, &WsServer::messageReceived, &instanceManager,
                     [&instanceManager](int instanceId, const Envelope& env) {
                         instanceManager.route(instanceId, env);
                     });

    // ── TrayManager (Wave 7 todo 13) ─────────────────────────────────────
    // QSystemTrayIcon wrapper. QtGui-only (QSystemTrayIcon lives in QtGui, so
    // it works under QGuiApplication — M3: NO QtWidgets link, NO QMenu).
    // Constructor calls QGuiApplication::setQuitOnLastWindowClosed(false)
    // unconditionally (blueprint §4.1.6) so the close-to-tray path works
    // even when no real tray is available. The QML Menu (Main.qml) is the
    // popup — TrayManager only emits requestContextMenu on right-click +
    // visibilityToggled on double-click; the menu items call the Q_INVOKABLE
    // activate*() methods. Declared BEFORE `engine` so it outlives the QML
    // context during stack unwind (engine destroyed first, then tray).
    TrayManager trayManager;

    // Wave 7 todo 14/15 — OS auto-launch (registry/.desktop) + the QML bridge
    // for PanelConfig's 4 behavior fields. Both declared BEFORE `engine` so
    // they outlive the QML context during stack unwind (engine destroyed
    // first, then these — same discipline as TrayManager). AutoLaunchManager
    // is zero-config (production ctor wires real suppliers); its
    // isEnabled/enable/disable are Q_INVOKABLE from SettingsPage.qml.
    // PanelConfigController takes configDir so its load-modify-save writes to
    // the same panel.json as WindowStateSaver + InstanceManager.
    AutoLaunchManager autoLaunchManager;
    PanelConfigController panelConfigController(ConfigDir::configDir());

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

    // Network stack context properties (Phase 5, todo 1). Exposed globally so
    // QML pages can observe WS state + pending-request diagnostics. Only
    // PendingRequests is shared (M2); StartupSalvo + ProcessManager stay
    // internal to C++ (no QML binding needed yet — todo 2 may expose them via
    // InstanceSession).
    engine.rootContext()->setContextProperty("wsServer", &wsServer);
    engine.rootContext()->setContextProperty("pendingRequests", &pendingRequests);

    // Instance roster context property (Phase 5, todo 7). Sidebar.qml binds
    // `model: instanceManager` directly to the QAbstractListModel; the Add /
    // Delete flows call its Q_INVOKABLE createInstance / requestDelete /
    // deleteInstance. Exposed AFTER the engine so the same stack-ordering
    // discipline as envChecker / windowStateSaver applies (engine destroyed
    // first, then the model during stack unwind — the model outlives QML).
    engine.rootContext()->setContextProperty("instanceManager", &instanceManager);

    // TrayManager context property (Wave 7 todo 13). Main.qml's Connections
    // block catches requestContextMenu / visibilityToggled / showSettings /
    // quitRequested; the QML Menu items call trayManager.activate*().
    engine.rootContext()->setContextProperty("trayManager", &trayManager);

    // AutoLaunchManager context property (Wave 7 todo 14/15). SettingsPage.qml
    // binds a Checkbox to autoLaunch.isEnabled() + calls enable()/disable() on
    // toggle. isEnabled() reads the live registry state (not the cached
    // PanelConfig.autoLaunchSystem flag) so the checkbox reflects external
    // changes (e.g. user edited the registry directly).
    engine.rootContext()->setContextProperty("autoLaunch", &autoLaunchManager);

    // PanelConfigController context property (Wave 7 todo 15). SettingsPage.qml
    // binds closeAction/confirmOnExit/startMinimized/autoLaunchSystem as
    // two-way Q_PROPERTY bindings; Main.qml's onClosing reads
    // panelConfig.closeAction + panelConfig.confirmOnExit to decide minimize-
    // to-tray vs confirm-then-exit.
    engine.rootContext()->setContextProperty("panelConfig", &panelConfigController);

    // Start the WS server on the hardcoded protocol port (blueprint §3.1).
    // A listen failure is non-fatal — the panel still opens; todo 11 adds
    // multi-instance + proper error handling. WsServer::listen already logs
    // "WsServer: listening on 127.0.0.1:<port>" on success (matches the
    // acceptance regex WsServer.*listen.*9001).
    constexpr quint16 kWsPort = 9001;
    if (!wsServer.listen(kWsPort)) {
        LOG_WARN("WsServer failed to listen on port {} — panel will open "
                 "without network (todo 11 adds error handling)", kWsPort);
    }

    // Register InstanceSession as a QML type so Q_INVOKABLE methods returning
    // InstanceSession* (InstanceManager::instanceAt) cross the C++→QML boundary.
    // Without registration the call yields "Unknown method return type:
    // InstanceSession*" and the Sidebar→detail-page switch silently no-ops.
    // Uncreatable: InstanceSession is constructed only by InstanceManager (C++),
    // never from QML. The matching Q_DECLARE_METATYPE(InstanceSession*) in
    // InstanceSession.hpp is necessary but not sufficient — this runtime
    // registration is the missing piece.
    qmlRegisterUncreatableType<InstanceSession>(
        "DesktopPet", 1, 0, "InstanceSession",
        QStringLiteral("InstanceSession is created only by InstanceManager (C++)"));

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    const int exitCode = app.exec();
    // Flush the rotating file sink so the final log lines (incl. the close
    // handler's "Saved window state") reach disk before process tear-down.
    Logging::shutdown();
    return exitCode;
}
