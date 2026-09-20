#include "app/PanelUiBoot.hpp"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QUrl>
#include <QtQml/QtQml>

#include <chrono>

#include <spdlog/spdlog.h>

#include "app/PanelApplication.hpp"
#include "core/AssetManager.hpp"
#include "core/ConfigDir.hpp"
#include "core/EnvironmentChecker.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/DownloadService.hpp"
#include "core/PanelConfigController.hpp"
#include "core/PluginHost.hpp"
#include "core/PluginManager.hpp"
#include "core/PluginPageModel.hpp"
#include "core/WindowStateSaver.hpp"
#include "logging/Logging.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"
#include "system/AutoLaunchManager.hpp"
#include "system/TrayManager.hpp"
#include "ui/NotificationStreamController.hpp"
#include "ui/ModelController.hpp"
#include "ui/VoicePackController.hpp"

// Moved from main() (P3/M3). All 17 context property names and their
// registration order are byte-identical to the old main() sequence — QML is
// untouched. Log strings keep their historical prefixes for P0 baselines /
// CI log scraping.

PanelUiBoot* PanelUiBoot::registerAll(QQmlApplicationEngine& engine,
                                      PanelApplication& app,
                                      qint64 coldStartT0Ms)
{
    // Parented to the engine — the mount-point note below explains the
    // teardown ordering this buys.
    return new PanelUiBoot(engine, app, coldStartT0Ms, &engine);
}

NotificationStreamController* PanelUiBoot::notificationStream()
{
    return m_notificationStream;
}

PanelUiBoot::PanelUiBoot(QQmlApplicationEngine& engine, PanelApplication& app,
                         qint64 coldStartT0Ms, QObject* parent)
    : QObject(parent), m_engine(engine), m_app(app)
{
    // ── Mount point / destruction order (M3, §B.2 v2) ─────────────────────
    // Every bridge below is a heap child of this PanelUiBoot, which is a
    // child of the QQmlApplicationEngine (see registerAll). Old stack layout:
    // bridges declared BEFORE the engine → destroyed AFTER the engine, BEFORE
    // the PanelApplication service tree. New layout: bridges die DURING
    // engine destruction — as QObject children they are deleted in ~QObject,
    // AFTER the engine's own QML internals (context / incubation teardown),
    // so each bridge still outlives the QML context it serves; and the
    // engine (stack object in main) still dies before the PanelApplication
    // service tree, so the bridge-vs-service relative order is UNCHANGED
    // (services last, exactly as before). Within this tree children die
    // registration-reverse — Qt 6.6+ implementation behavior, NOT a
    // documented contract (§B.2 v2 annotation). The per-bridge "Lifetime
    // (M3)" notes below replace the old dead "declared BEFORE engine"
    // stack-discipline sentences; everything else in those comments is
    // verbatim.

    // ── TrayManager (Wave 7 todo 13) ─────────────────────────────────────
    // QSystemTrayIcon wrapper. QtGui-only (QSystemTrayIcon lives in QtGui, so
    // it works under QGuiApplication — M3: NO QtWidgets link, NO QMenu).
    // Constructor calls QGuiApplication::setQuitOnLastWindowClosed(false)
    // unconditionally (blueprint §4.1.6) so the close-to-tray path works
    // even when no real tray is available. The QML Menu (Main.qml) is the
    // popup — TrayManager only emits requestContextMenu on right-click +
    // visibilityToggled on double-click; the menu items call the Q_INVOKABLE
    // activate*() methods. Lifetime (M3): heap child of PanelUiBoot — see
    // the mount-point note above.
    m_trayManager = new TrayManager(this);

    // Wave 7 todo 14/15 — OS auto-launch (registry/.desktop) + the QML bridge
    // for PanelConfig's 4 behavior fields. AutoLaunchManager
    // is zero-config (production ctor wires real suppliers); its
    // isEnabled/enable/disable are Q_INVOKABLE from SettingsPage.qml.
    // PanelConfigController takes configDir so its load-modify-save writes to
    // the same panel.json as WindowStateSaver + InstanceManager.
    // Lifetime (M3): heap children of PanelUiBoot — see mount-point note.
    m_autoLaunchManager = new AutoLaunchManager(this);
    m_panelConfigController = new PanelConfigController(ConfigDir::configDir(),
                                                        this);
    m_panelConfigController->setDatabase(&m_app.databaseManager());

    // AssetManager (image library + logo/instance-icon refs). Shares the
    // DatabaseManager; exposed as the "assetManager" context property for
    // AssetPage / SettingsPage / InstanceDetailPage.
    // Lifetime (M3): heap child of PanelUiBoot — see mount-point note.
    m_assetManager = new AssetManager(m_app.databaseManager(),
                                      ConfigDir::configDir(), this);
    m_app.instanceManager()->setAssetRefDetacher(
        [this](const QString& uuid) {
            m_assetManager->detachInstanceIconRefs(uuid);
        });

    // ── Notification bubble stream (气泡信息流) ─────────────────────────
    // Lifetime (M3): heap child of PanelUiBoot, same discipline as
    // TrayManager/PanelConfigController — see mount-point note.
    // Owns the shared bubble model; registered as the
    // "notificationStream" context property right after the engine exists
    // (below). The dialogue sink seam lets InstanceSession push bubbles
    // through the same stream without depending on this controller's type —
    // voice-pack behavior dialogue is the ONLY bubble text source (design
    // revision: no dialogue-pack system).
    m_notificationStream = new NotificationStreamController(this);
    m_app.instanceManager()->setDialogueSink(
        [this](const QString& instanceId, const QString& name,
               const QString& avatar, const QString& text,
               int durationMs) {
            Q_UNUSED(instanceId);
            m_notificationStream->push(name, avatar, text, durationMs);
        });

    // Required BEFORE any QQuickWindow is created: enables alpha in the
    // window surface format so BubbleStreamWindow's color "transparent"
    // actually composites against the desktop instead of an opaque black
    // surface (Qt docs, "Window and view coordinates"/translucency note).
    // (M3: still holds — registerAll runs after the engine OBJECT exists but
    // before loadFromModule creates any window.)
    QQuickWindow::setDefaultAlphaBuffer(true);

    // EnvironmentChecker (T27) — exposed as a global QML context property
    // "envChecker" so every page can read the readiness probes without
    // per-page instantiation. Lifetime (M3): heap child of PanelUiBoot — see
    // mount-point note.
    // WelcomePage.qml calls envChecker.runChecks() on Component.onCompleted.
    m_envChecker = new EnvironmentChecker(this);
    m_engine.rootContext()->setContextProperty("envChecker", m_envChecker);

    // T28: pass the restored geometry + theme as individual QML context
    // properties so Main.qml can bind its x/y/width/height + call
    // Theme.setTheme(initialTheme) on startup. panelX/Y of -1 means "center on
    // screen" (PanelConfig.hpp); Main.qml derives the centered coordinates from
    // Screen.width/height when it sees the -1 sentinel.
    m_engine.rootContext()->setContextProperty("initialX",
                                               m_app.panelConfig().panelX);
    m_engine.rootContext()->setContextProperty("initialY",
                                               m_app.panelConfig().panelY);
    m_engine.rootContext()->setContextProperty("initialWidth",
                                               m_app.panelConfig().panelWidth);
    m_engine.rootContext()->setContextProperty("initialHeight",
                                               m_app.panelConfig().panelHeight);
    m_engine.rootContext()->setContextProperty("initialTheme",
                                               m_app.panelConfig().theme);

    // WindowStateSaver (T28) — Q_INVOKABLE saveWindowState called from
    // Main.qml's onClosing handler and from a Theme.themeChanged Connections
    // block. The onClosing handler fires during engine/window teardown and
    // must reach a live saver. Lifetime (M3): heap child of PanelUiBoot —
    // dies during engine destruction but AFTER the engine's own QML
    // internals (QObject children are deleted last), so the saver still
    // outlives the context it serves; see mount-point note.
    m_windowStateSaver = new WindowStateSaver(ConfigDir::configDir(), this);
    m_engine.rootContext()->setContextProperty("windowStateSaver",
                                               m_windowStateSaver);

    // Network stack context properties (Phase 5, todo 1). Exposed globally so
    // QML pages can observe WS state + pending-request diagnostics. Only
    // PendingRequests is shared (M2); StartupSalvo + ProcessManager stay
    // internal to C++ (no QML binding needed yet — todo 2 may expose them via
    // InstanceSession).
    m_engine.rootContext()->setContextProperty("wsServer", m_app.wsServer());
    m_engine.rootContext()->setContextProperty("pendingRequests",
                                               m_app.pendingRequests());

    // Instance roster context property (Phase 5, todo 7). Sidebar.qml binds
    // `model: instanceManager` directly to the QAbstractListModel; the Add /
    // Delete flows call its Q_INVOKABLE createInstance / requestDelete /
    // deleteInstance. The model itself lives in the PanelApplication service
    // tree (M2) — destroyed after the engine by main()'s stack-reverse, so it
    // still outlives QML.
    m_engine.rootContext()->setContextProperty("instanceManager",
                                               m_app.instanceManager());

    // TrayManager context property (Wave 7 todo 13). Main.qml's Connections
    // block catches requestContextMenu / visibilityToggled / showSettings /
    // quitRequested; the QML Menu items call trayManager.activate*().
    m_engine.rootContext()->setContextProperty("trayManager", m_trayManager);

    // AutoLaunchManager context property (Wave 7 todo 14/15). SettingsPage.qml
    // binds a Checkbox to autoLaunch.isEnabled() + calls enable()/disable() on
    // toggle. isEnabled() reads the live registry state (not the cached
    // PanelConfig.autoLaunchSystem flag) so the checkbox reflects external
    // changes (e.g. user edited the registry directly).
    m_engine.rootContext()->setContextProperty("autoLaunch",
                                               m_autoLaunchManager);

    // PanelConfigController context property (Wave 7 todo 15). SettingsPage.qml
    // binds closeAction/confirmOnExit/startMinimized/autoLaunchSystem as
    // two-way Q_PROPERTY bindings; Main.qml's onClosing reads
    // panelConfig.closeAction + panelConfig.confirmOnExit to decide minimize-
    // to-tray vs confirm-then-exit.
    m_engine.rootContext()->setContextProperty("panelConfig",
                                               m_panelConfigController);

    // Notification bubble stream context property. BubbleStreamWindow.qml
    // binds visible to notificationStream.count/enabled and its Repeater to
    // notificationStream.model; QML calls push/dismiss/testBubble on it.
    m_engine.rootContext()->setContextProperty("notificationStream",
                                               m_notificationStream);

    // VoicePackController context property. Discovery over VoicePackScanner
    // + MetaMkoParser; todo 21 mount wiring reaches the per-instance
    // MountedBehaviorEngine via the InstanceManager.
    // Lifetime (M3): heap child of PanelUiBoot — see mount-point note.
    m_voicePackController = new VoicePackController(this);
    m_voicePackController->setInstanceManager(m_app.instanceManager());
    m_engine.rootContext()->setContextProperty("voicePacks",
                                               m_voicePackController);

    // ModelController context property (模型库 page backend). Discovery over
    // ModelScanner + ModelInfoParser; the rendererDir injection mirrors the
    // SAME source VoicePackController derives internally
    // (QCoreApplication::applicationDirPath — the build places the controller
    // and renderer exes side-by-side in build/bin, so the renderer's
    // Resources/Models tree hangs off the app dir).
    // Lifetime (M3): heap child of PanelUiBoot — see mount-point note.
    m_modelController = new ModelController(this);
    m_modelController->setRendererDir(QCoreApplication::applicationDirPath());
    m_engine.rootContext()->setContextProperty("modelLibrary",
                                               m_modelController);

    // ── Plugin bridges (P4) — context properties #18/#19 ────────────────
    // Mounted AFTER the original 17 so their slots are append-only. The
    // page model must exist before PluginHost::initializeAll() (main calls
    // it after this boot) — plugins' registerPage() calls land in it; the
    // manager supplies the boot-time enabled provider (kv-backed). Both
    // are heap children of this PanelUiBoot (mount-point note above): they
    // die during engine destruction, AFTER the QML context — same
    // lifetime discipline as every other bridge. The plugin INSTANCES
    // themselves live in the PanelApplication service tree (destroyed
    // later, §B.2 v2 cross-subtree ordering).
    m_pluginPageModel = new core::PluginPageModel(this);
    m_pluginManager = new core::PluginManager(m_app.pluginRegistry(),
                                              &m_app.databaseManager(), this);
    m_app.pluginHost()->setPageModel(m_pluginPageModel);
    m_app.pluginHost()->setNotificationStream(m_notificationStream);
    m_app.pluginHost()->setEnabledProvider(
        [this](const QString& pluginId) {
            return m_pluginManager->enabledAtStart(pluginId);
        });
    // P5 (§B.4 sentinel → refresh chain): a finished pack install re-scans
    // the pack list (VoicePacks page + plugin voicePackApi observers), and
    // the plugin-side refreshScan() reaches the same controller.
    m_app.pluginHost()->setVoicePackRefresh(
        [this]() { m_voicePackController->rescan(); });
    QObject::connect(m_app.downloadService(), &core::DownloadService::packInstalled,
                     m_voicePackController,
                     [this](const QString& packName) {
                         LOG_INFO("voice packs re-scanned after install of '{}'",
                                  packName.toStdString());
                         m_voicePackController->rescan();
                     });
    m_engine.rootContext()->setContextProperty("pluginPages",
                                               m_pluginPageModel);
    m_engine.rootContext()->setContextProperty("pluginManager",
                                               m_pluginManager);

    // AssetManager context property (资源管理 page + Settings logo section +
    // InstanceDetailPage icon picker). Logo applies to THREE surfaces:
    //   1. taskbar/window icon — QGuiApplication::setWindowIcon (default for
    //      every QQuickWindow, incl. the Main.qml panel)
    //   2. tray icon — TrayManager::setIconPixmap, gated by logo_sync_tray
    //   3. QML bindings read assetManager.logoUrl directly (titlebar/nav)
    // Unconditional for 1 (the user explicitly applied a logo), opt-in for 2.
    const auto applyLogo = [this]() {
        const QString file = QUrl(m_assetManager->logoUrl()).toLocalFile();
        if (!file.isEmpty()) {
            QGuiApplication::setWindowIcon(QIcon(file));
        }
        if (m_assetManager->logoSyncTray() && !file.isEmpty()) {
            m_trayManager->setIconPixmap(file);
        }
    };
    m_engine.rootContext()->setContextProperty("assetManager", m_assetManager);
    applyLogo();
    QObject::connect(m_assetManager, &AssetManager::logoUrlChanged,
                     m_trayManager, applyLogo);

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
    m_envChecker->setOwnServerListening(m_app.wsServer()->listen(kWsPort));
    if (!m_envChecker->portBindable()) {
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
    // evaluates Component.onCompleted. (M3: the anchor VALUE is still
    // captured before QGuiApplication in main() and passed in; only the
    // registration moved.)
    m_engine.rootContext()->setContextProperty("coldStartT0Ms", coldStartT0Ms);

    // T24: log the C++ side pre-load cost (everything between main() entry
    // and the QML load call). The QML side logs the residual (loadFromModule
    // cost + first-frame). Their sum is the wall-clock cold-start number.
    // (M4: moved from main — this is the last statement before main calls
    // loadFromModule, so the measured span is unchanged.)
    {
        const auto nowSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        const qint64 nowMs = static_cast<qint64>(
            std::chrono::duration_cast<std::chrono::milliseconds>(nowSinceEpoch).count());
        LOG_INFO("COLD_START_PRE_LOAD_MS={} t0={}", nowMs - coldStartT0Ms, coldStartT0Ms);
    }
}
