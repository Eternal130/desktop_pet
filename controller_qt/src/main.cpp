#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/QtQml>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>

#include <cstdlib>
#include <chrono>
#include <functional>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

#include "app/ScreenshotRunner.hpp"
#include "app/PanelApplication.hpp"
#include "core/ConfigDir.hpp"
#include "core/DatabaseManager.hpp"
#include "core/AssetManager.hpp"
#include "core/EnvironmentChecker.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelConfigController.hpp"
#include "ui/VoicePackController.hpp"
#include "core/PanelStateManager.hpp"
#include "core/WindowStateSaver.hpp"
#include "core/StartupSalvo.hpp"
#include "core/ProcessManager.hpp"
#include "ui/NotificationStreamController.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"
#include "network/PendingRequests.hpp"
#include "system/AutoLaunchManager.hpp"
#include "system/TrayManager.hpp"

// ── CJK default-font guard (tofu fix) ─────────────────────────────────────
// Hosts without a system CJK font (fc-list :lang=zh empty — the dev machine
// that reported the bug) render every Chinese glyph as tofu; English text
// works because the Latin fallback resolves. Ship Noto Sans CJK SC next to
// the exe (<appDir>/fonts/, deployed by the controller's own POST_BUILD in
// CMakeLists.txt) and make it the application default with the previous
// default family kept as fallback (Latin glyphs still resolve there first
// if it ranked higher). Never fatal — any failure logs a WARN and keeps the
// system default (§9.5 never-crash contract). Called before engine.load so
// screenshot mode benefits identically.
static void installDefaultFontWithCjk()
{
    const QString fontPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/fonts/NotoSansCJKsc-Regular.otf");

    // Qt 6: QFontDatabase is a static-only API surface.
    const int fontId = QFontDatabase::addApplicationFont(fontPath);
    if (fontId < 0) {
        LOG_WARN("main: CJK font not loaded from '{}' — Chinese text may "
                 "render as tofu on hosts without a system CJK font",
                 fontPath.toStdString());
        return;
    }
    const QStringList families =
        QFontDatabase::applicationFontFamilies(fontId);
    if (families.isEmpty()) {
        LOG_WARN("main: CJK font '{}' loaded but exposed no family — "
                 "keeping system default", fontPath.toStdString());
        return;
    }

    // Copy the current default (keeps point size / style hints) and prepend
    // the CJK family: ["Noto Sans CJK SC", <original default>].
    QFont font = QGuiApplication::font();
    QStringList familiesChain{families.first()};
    const QString originalFamily = font.family();
    if (!originalFamily.isEmpty() && !familiesChain.contains(originalFamily))
        familiesChain.append(originalFamily);
    font.setFamilies(familiesChain);
    QGuiApplication::setFont(font);
    LOG_INFO("main: CJK default font installed ('{}' -> family '{}') — "
             "tofu guard active",
             fontPath.toStdString(), families.first().toStdString());
}

int main(int argc, char *argv[])
{
    // Use the Basic QuickControls style: the default native (Windows) style
    // forbids customizing Control background/contentItem, which (a) breaks
    // Theme-bound CheckBox/ComboBox colors and (b) crashes the renderer when
    // QtCharts' ChartView loads under a ComboBox with a custom background.
    // Basic imposes no palette of its own, so our Theme singleton owns all
    // visuals. Must be set before QGuiApplication construction.
    // Ref: https://doc.qt.io/qt-6/qtquickcontrols2-styles.html
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

    // ── T24 cold-start timing anchor ─────────────────────────────────────
    // Captured BEFORE QGuiApplication construction so Qt framework init cost
    // is included in the cold-start delta. Exposed to QML as the
    // `coldStartT0Ms` context property so Main.qml's Component.onCompleted can
    // log the wall-clock-visible delta ("COLD_START_MS=<n>"). std::chrono is
    // used (not QDateTime) because it is provably safe pre-QCoreApplication.
    // The matching log line in Main.qml is the measurement point; this anchor
    // is only set once per process and never read again after Component.
    // onCompleted logs the delta.
    const auto t0SinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const qint64 coldStartT0Ms = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t0SinceEpoch).count());

    // QApplication (NOT QGuiApplication): Qt 6.10 QtCharts' ChartView depends
    // on QtWidgets initialization — under a bare QGuiApplication the ChartView
    // creation crashes with 0xC0000005 inside Qt6Widgets.dll (MonitorPage).
    // Verified by minimal repro: QGuiApplication+ChartView crashes,
    // QApplication+ChartView renders fine. QtWidgets is already linked for
    // QSystemTrayIcon (T13), so this costs nothing extra.
    QApplication app(argc, argv);

    // Boot logging BEFORE any LOG_* call. ConfigDir::ensureDirectories creates
    // ~/.config/desktop-pet/{,instances,logs}/ so Logging::init can attach its
    // rotating file sink. A failure to create the tree is non-fatal (Logging
    // degrades to console-only — see Logging.hpp). A failed init() is likewise
    // non-fatal: it already installed the console sink + Qt message bridge.
    ConfigDir::ensureDirectories();
    Logging::init(ConfigDir::logsDir());

    // CJK tofu guard: must run before the QML engine loads so every default-
    // fonted Text element (and screenshot mode) renders Chinese. See the
    // utility above; WARN-only on failure.
    installDefaultFontWithCjk();

    // ── Application service tree (P3/M2) ─────────────────────────────────
    // DatabaseManager / PanelStateManager / WsServer / PendingRequests /
    // InstanceManager and all their wiring now live in app/PanelApplication
    // — a QObject parent tree (heap + parent) that replaces the stack-reverse
    // discipline for these five; construction and connect order are the
    // original main() order. Declared here BEFORE every UI bridge and the
    // engine, so by stack-reverse the whole tree is destroyed after the QML
    // context (the cross-subtree invariant — the §B.2 v2 annotation in
    // PanelApplication.cpp covers what holds inside the tree). Context-
    // property registration stays in main until M3; the aliases below are
    // the seam.
    PanelApplication panelApp;
    DatabaseManager& databaseManager = panelApp.databaseManager();
    WsServer* const wsServer = panelApp.wsServer();
    PendingRequests* const pendingRequests = panelApp.pendingRequests();
    InstanceManager* const instanceManager = panelApp.instanceManager();
    const PanelConfig panelCfg = panelApp.panelConfig();

    // StartupSalvo + ProcessManager: Phase-5 single-instance holders wired
    // here for sender injection; todo 2 (InstanceSession) owns them
    // per-instance. These globals are now redundant (InstanceSession owns
    // its own), but they are harmless and NetworkWiringTest asserts the
    // sender wiring, so they stay until a dedicated cleanup task. (M2 note:
    // they used to be constructed between PendingRequests and the roster;
    // with the service tree in one ctor they now construct after it — inert
    // reorder, nothing between the old points observes them and no renderer
    // connects until the event loop runs.)
    StartupSalvo startupSalvo;
    ProcessManager processManager;

    // Wire sender-injection seams → WsServer::sendText(instanceId, ...). These
    // legacy globals always targeted instance_id 0 (Phase 0-4 single-instance
    // assumption); InstanceSession owns its own per-instance senders below.
    startupSalvo.setCommandSender([wsServer](const QString& json) {
        wsServer->sendText(0, json);
    });
    processManager.setShutdownSender([wsServer]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        wsServer->sendText(0, QString::fromUtf8(json));
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
    panelConfigController.setDatabase(&databaseManager);

    // AssetManager (image library + logo/instance-icon refs). Shares the
    // DatabaseManager; exposed as the "assetManager" context property for
    // AssetPage / SettingsPage / InstanceDetailPage.
    AssetManager assetManager(databaseManager, ConfigDir::configDir());
    instanceManager->setAssetRefDetacher(
        [&assetManager](const QString& uuid) {
            assetManager.detachInstanceIconRefs(uuid);
        });

    // ── Notification bubble stream (气泡信息流) ─────────────────────────
    // Declared BEFORE the engine (same stack-ordering discipline as
    // TrayManager/PanelConfigController above): the engine is destroyed
    // first during stack unwind, so the bubble stream outlives the QML
    // context. Owns the shared bubble model; registered as the
    // "notificationStream" context property right after the engine exists
    // (below). The dialogue sink seam lets InstanceSession push bubbles
    // through the same stream without depending on this controller's type —
    // voice-pack behavior dialogue is the ONLY bubble text source (design
    // revision: no dialogue-pack system).
    NotificationStreamController notificationStream;
    instanceManager->setDialogueSink(
        [&notificationStream](const QString& instanceId, const QString& name,
                              const QString& avatar, const QString& text,
                              int durationMs) {
            Q_UNUSED(instanceId);
            notificationStream.push(name, avatar, text, durationMs);
        });

    // Required BEFORE any QQuickWindow is created: enables alpha in the
    // window surface format so BubbleStreamWindow's color "transparent"
    // actually composites against the desktop instead of an opaque black
    // surface (Qt docs, "Window and view coordinates"/translucency note).
    QQuickWindow::setDefaultAlphaBuffer(true);

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
    engine.rootContext()->setContextProperty("wsServer", wsServer);
    engine.rootContext()->setContextProperty("pendingRequests", pendingRequests);

    // Instance roster context property (Phase 5, todo 7). Sidebar.qml binds
    // `model: instanceManager` directly to the QAbstractListModel; the Add /
    // Delete flows call its Q_INVOKABLE createInstance / requestDelete /
    // deleteInstance. Exposed AFTER the engine so the same stack-ordering
    // discipline as envChecker / windowStateSaver applies (engine destroyed
    // first, then the model during stack unwind — the model outlives QML).
    engine.rootContext()->setContextProperty("instanceManager", instanceManager);

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

    // Notification bubble stream context property. BubbleStreamWindow.qml
    // binds visible to notificationStream.count/enabled and its Repeater to
    // notificationStream.model; QML calls push/dismiss/testBubble on it.
    engine.rootContext()->setContextProperty("notificationStream", &notificationStream);

    // VoicePackController context property. Discovery over VoicePackScanner
    // + MetaMkoParser; todo 21 mount wiring reaches the per-instance
    // MountedBehaviorEngine via the InstanceManager.
    VoicePackController voicePackController;
    voicePackController.setInstanceManager(instanceManager);
    engine.rootContext()->setContextProperty("voicePacks", &voicePackController);

    // AssetManager context property (资源管理 page + Settings logo section +
    // InstanceDetailPage icon picker). Logo applies to THREE surfaces:
    //   1. taskbar/window icon — QGuiApplication::setWindowIcon (default for
    //      every QQuickWindow, incl. the Main.qml panel)
    //   2. tray icon — TrayManager::setIconPixmap, gated by logo_sync_tray
    //   3. QML bindings read assetManager.logoUrl directly (titlebar/nav)
    // Unconditional for 1 (the user explicitly applied a logo), opt-in for 2.
    const auto applyLogo = [&assetManager, &trayManager]() {
        const QString file = QUrl(assetManager.logoUrl()).toLocalFile();
        if (!file.isEmpty()) {
            QGuiApplication::setWindowIcon(QIcon(file));
        }
        if (assetManager.logoSyncTray() && !file.isEmpty()) {
            trayManager.setIconPixmap(file);
        }
    };
    engine.rootContext()->setContextProperty("assetManager", &assetManager);
    applyLogo();
    QObject::connect(&assetManager, &AssetManager::logoUrlChanged,
                     &trayManager, applyLogo);

    // Start the WS server on the hardcoded protocol port (blueprint §3.1).
    // A listen failure is non-fatal — the panel still opens; todo 11 adds
    // multi-instance + proper error handling. WsServer::listen already logs
    // "WsServer: listening on 127.0.0.1:<port>" on success (matches the
    // acceptance regex WsServer.*listen.*9001).
    constexpr quint16 kWsPort = 9001;
    // Own-listener fact for the port probe: our WsServer holding 9001 is
    // readiness, not a conflict (EnvironmentChecker override — see .hpp).
    // A genuine listen failure (another process owns 9001) leaves the
    // override false so the probe still reports the conflict.
    envChecker.setOwnServerListening(wsServer->listen(kWsPort));
    if (!envChecker.portBindable()) {
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

    // T24: expose the cold-start anchor to QML so Main.qml's Component.
    // onCompleted can compute the user-visible delta. Set BEFORE
    // loadFromModule so the property is in place by the time the QML engine
    // evaluates Component.onCompleted.
    engine.rootContext()->setContextProperty("coldStartT0Ms", coldStartT0Ms);

    // T24: log the C++ side pre-load cost (everything between main() entry
    // and the QML load call). The QML side logs the residual (loadFromModule
    // cost + first-frame). Their sum is the wall-clock cold-start number.
    {
        const auto nowSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        const qint64 nowMs = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(nowSinceEpoch).count());
        LOG_INFO("COLD_START_PRE_LOAD_MS={} t0={}", nowMs - coldStartT0Ms, coldStartT0Ms);
    }

    // Loads type "Main" from the QML module registered in CMakeLists.txt
    // (qt_add_qml_module, URI "DesktopPet").
    engine.loadFromModule("DesktopPet", "Main");

    // Auto-start (per-instance config.autoStart): launch every instance whose
    // flag is set once the QML UI is up — the renderer windows appear
    // alongside the panel. Queued via QTimer::singleShot(0) so the first
    // frame paints before the (blocking, process-spawning) start() calls run.
    QTimer::singleShot(0, [instanceManager]() {
        for (int i = 0; i < instanceManager->rowCount(); ++i) {
            InstanceSession* s = instanceManager->instanceAt(i);
            if (s != nullptr && s->autoStartEnabled()) {
                LOG_INFO("autoStart: launching instance \"{}\"",
                         s->label().toStdString());
                s->start();
            }
        }
    });

    // ── Visual-QA modes (--screenshot / --bubble) ─────────────────────────
    // Extracted verbatim to app/ScreenshotRunner (P3/M1). Sits exactly where
    // the inline block used to (after the auto-start singleShot registration,
    // before the normal run loop) so behavior is unchanged. Returns only when
    // no QA flag was passed; an active QA mode ends the process via
    // std::exit inside (teardown-history notes live in ScreenshotRunner.cpp).
    runScreenshotQa(app, engine, notificationStream, argc, argv);

    const int exitCode = app.exec();
    Logging::shutdown();
    return exitCode;
}
