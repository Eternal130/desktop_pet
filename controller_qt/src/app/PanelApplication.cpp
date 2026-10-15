#include "app/PanelApplication.hpp"

#include <QJsonDocument>
#include <QTimer>

#include <spdlog/spdlog.h>

#include "core/ConfigDir.hpp"
#include "core/DatabaseManager.hpp"
#include "core/EventBus.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PathResolve.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginHost.hpp"
#include "core/ProcessManager.hpp"
#include "core/DownloadService.hpp"
#include "core/RuleEngineService.hpp"
#include "core/StartupSalvo.hpp"
#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

#include <QAbstractItemModel>

// Moved from main() (P3/M2). Construction and connect order below are the
// original main() order, statement for statement; only the storage form
// changed (stack objects → heap objects parented to `this`). The log strings
// keep their historical "main:" prefix verbatim — P0 baselines and CI log
// scraping depend on them.

PanelApplication::PanelApplication(QObject* parent)
    : QObject(parent)
{
    // Storage-layout revision: one-time legacy-config migration before the
    // first config read below (DatabaseManager opens <configDir>/app.db,
    // which would make a fresh target dir look "already populated").
    // main() already ran it before the ensureDirectories() scaffold; this
    // second call is an idempotent no-op (Windows: legacy dir no longer
    // exists after the first run; other platforms: compile-time false) that
    // guards any embedding constructing this tree without main()'s
    // sequencing. ensureDirectories() stays a single call in main() — it
    // must precede Logging::init, which needs logs/.
    ConfigDir::migrateLegacyIfNeeded();

    // ── SQLite config backend ─────────────────────────────────────────────
    // ONE DatabaseManager owns <configDir>/app.db; every config consumer
    // (PanelStateManager, InstanceConfigManager, AssetManager) shares it by
    // reference. A failed open is non-fatal — the managers degrade to
    // defaults / failed writes (never-throws contract).
    m_databaseManager = new DatabaseManager(this);
    if (!m_databaseManager->open(ConfigDir::configDir() +
                                 QStringLiteral("app.db"))) {
        LOG_WARN("main: app.db open failed — config persistence degraded");
    }

    // Load the persisted panel-level config (T28 window-state restore) so the
    // QML window can restore its initial position/size + theme on startup.
    // SQLite backend (panel_config kv); empty db → defaults.
    m_panelStateManager = new PanelStateManager({}, this);
    m_panelStateManager->setDatabase(m_databaseManager);
    m_panelConfig = m_panelStateManager->load();
    LOG_INFO("Restored panel config: panelX={} panelY={} {}x{} theme=\"{}\"",
             m_panelConfig.panelX, m_panelConfig.panelY,
             m_panelConfig.panelWidth, m_panelConfig.panelHeight,
             m_panelConfig.theme.toStdString());

    // ── Network stack (Phase 5, todo 1) ───────────────────────────────────
    // WsServer: the WS server socket (renderer connects here on 127.0.0.1:9001).
    // PendingRequests: GLOBAL id→result table (M2 resolution — instance-agnostic;
    // response id matching must work across all instances, so this is the ONLY
    // network object shared globally; each InstanceSession owns its own
    // MessageDispatcher + EventRegistry).
    // (StartupSalvo + ProcessManager used to be declared between these and the
    // roster below; they are NOT part of this service tree — they stay in
    // main(), constructed after this PanelApplication. Inert reorder: nothing
    // between the old points observed them and no renderer connects until the
    // event loop runs.)
    //
    // Lifetime: these used to be stack objects declared BEFORE the engine so
    // they outlived it during stack unwind. Now heap children of this
    // PanelApplication, which main() declares before every UI bridge and the
    // engine — same "outlives the QML context" guarantee, held at the main()
    // stack level.
    m_wsServer = new WsServer(this);
    m_pendingRequests = new PendingRequests(this);

    // ── Shared event bus + events API (S8a, v1.5) ──────────────────────
    // Constructed BEFORE the InstanceManager (unlike the v1.2-v1.4
    // shared APIs further down, which follow it): every session holds
    // the publisher pointer, so teardown order requires the bus to die
    // AFTER the manager (and thus after every session). The bus depends
    // on nothing, so the early registration is safe. The API face wraps
    // the bus; PluginHost receives it below (plugin contexts get the
    // per-plugin proxy, never this raw impl).
    m_eventBus = new core::EventBus(this);
    m_eventApi = new core::EventApiImpl(m_eventBus, this);

    // ── Instance roster (Phase 5, todo 7) ────────────────────────────────
    // InstanceManager owns every InstanceSession, loads existing instances
    // from panel.json instanceIds order on construction (m4 fix), and persists
    // roster mutations via the injected savePanel callback (m5 fix — wired to
    // PanelStateManager::save so create/delete reach panel.json immediately).
    //
    // Ordering + ownership (M2 REWRITE of the old stack-ordering note): the
    // manager is still constructed AFTER psm/wsServer/pendingRequests and
    // BEFORE the engine — same relative position as when everything lived on
    // main()'s stack. What changed is ownership: the old "parent=nullptr on
    // the manager itself" guarded against stack-object + Qt-parent
    // double-delete during stack unwind; that premise is GONE with the move
    // to a parent tree — the manager is heap-allocated and parented to this
    // PanelApplication, so the parent owns it and there is exactly one
    // delete. The INTERNAL ownership one level down is UNCHANGED and must
    // stay: InstanceManager deletes its InstanceSession list via qDeleteAll
    // (InstanceManager.cpp), so those Session objects remain parent=nullptr —
    // giving them a Qt parent would double-delete.
    //
    // Destruction-order FACT (2026-10 teardown diagnosis; direction
    // corrected from the §B.2 v2 annotation): QObject children die in
    // REGISTRATION order on Qt >= 6.6 — first registered dies FIRST
    // (empirically verified on Qt 6.10.2; NOT a documented contract).
    // Registration order in this ctor mirrors the old declaration order
    // (databaseManager → psm → wsServer → pendingRequests →
    // instanceManager), so the DEFAULT cascade tears down in that same
    // forward order — NOT the old stack-reverse sequence. Where a
    // later-registered service's destructor still calls an
    // earlier-registered one, ~PanelApplication() deletes those explicitly
    // beforehand (exit-path use-after-free fix). The CROSS-subtree order —
    // this service tree vs main()'s UI bridges and the engine — is governed
    // by main()'s stack, not by this class: PanelApplication is declared
    // before all of them, so it is destroyed after them.
    m_instanceManager = new InstanceManager(
        ConfigDir::configDir(), *m_wsServer, *m_pendingRequests,
        [this](const PanelConfig& cfg) { m_panelStateManager->save(cfg); },
        this);
    m_instanceManager->setDatabase(m_databaseManager);

    // S8a (v1.5): hand every session the shared event publisher (bypass
    // emissions). Sessions restored from disk already exist; rowsInserted
    // covers every later create. The publisher is a NOTIFICATION-ONLY,
    // pet::IEventApi-typed seam (interface pointer — NOT the concrete
    // impl — so tests inject fakes); plain C++ setter, deliberately no
    // Q_INVOKABLE / Q_PROPERTY WRITE (api-boundary gate pattern table
    // stays untouched, setDialogueSink discipline). Null-guarded inside
    // every emission site — sessions without a publisher behave exactly
    // as pre-S8a.
    QObject::connect(m_instanceManager, &QAbstractItemModel::rowsInserted,
                     this,
                     [this](const QModelIndex&, int first, int last) {
                         for (int row = first; row <= last; ++row) {
                             InstanceSession* s =
                                 m_instanceManager->instanceAt(row);
                             if (s != nullptr)
                                 s->setEventPublisher(m_eventApi);
                         }
                     });
    for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
        InstanceSession* s = m_instanceManager->instanceAt(row);
        if (s != nullptr)
            s->setEventPublisher(m_eventApi);
    }

    // WsServer::messageReceived(int instanceId, env) → InstanceManager::route.
    // Phase 5 todo 11: per-instanceId demux — the WsServer carries the parsed
    // ?instance_id=N on the signal so route() forwards to the right session
    // directly (no Phase-5 single-instance row-0 hack). InstanceManager::route
    // does a linear scan by instanceId() (sidebar-sized N, O(N) is fine).
    QObject::connect(m_wsServer, &WsServer::messageReceived, m_instanceManager,
                     [this](int instanceId, const Envelope& env) {
                         m_instanceManager->route(instanceId, env);
                     });

    // ── Shared roster API (S3 dogfooding) ───────────────────────────────
    // ONE InstanceApiImpl for the whole panel: injected into every plugin
    // context (below, via the host) AND into the panel's own RosterApiModel
    // (PanelUiBoot) — the sidebar's read path and the plugin SDK's read path
    // converge on this single object. Registered AFTER the manager → dies
    // AFTER it (Qt ≥ 6.6 registration order — the old registration-reverse
    // claim had the direction backwards). The model-signal connections
    // still never dangle: they are QObject connections that drop
    // automatically when the sessions die with the manager, and
    // ~InstanceApiImpl makes no cross-calls. Zero core behavior change:
    // it only re-homes the object
    // that used to be a per-plugin PluginContextImpl member.
    m_instanceApi = new core::InstanceApiImpl(m_instanceManager, this);

    // ── Shared instance-control write API (S2, v1.2) ────────────────────
    // ONE InstanceControlApiImpl for the whole panel, same discipline as
    // the shared InstanceApiImpl above: injected into every plugin context
    // (PluginHost hands it to PluginContextImpl::queryApi — gated there by
    // the "instance_lifecycle" capability AND the live plugin_write_enabled
    // switch) and into the panel's own RosterApiModel write bridge (host
    // bridge, deliberately NOT capability-gated). Registered after
    // m_instanceApi → dies AFTER it (Qt ≥ 6.6 registration order); no
    // destructor cross-calls — safe.
    m_instanceControlApi = new core::InstanceControlApiImpl(m_instanceManager,
                                                            this);

    // ── Shared v1.3 families (S5/S6 dogfooding) ────────────────────────
    // ONE implementation per family for the whole panel, same discipline
    // as the shared InstanceApiImpl/InstanceControlApiImpl above. All
    // registered after m_instanceControlApi → destroyed after it, same
    // forward order (Qt ≥ 6.6 registration order — the old
    // registration-reverse claim had the direction backwards); none of
    // their destructors make cross-calls — safe. The model scan cache +
    // the voice-pack
    // source seams get their renderer-dir / host-scan wiring in
    // PanelUiBoot (they observe UI-adjacent singletons constructed
    // later); the settings impl shares this tree's DatabaseManager so
    // plugin writes and the panel's own settings page hit ONE db.
    m_tuningApi = new core::TuningApiImpl(m_instanceManager, this);
    m_modelApi = new core::ModelApiImpl(this);
    m_settingsApi = new core::SettingsApiImpl(ConfigDir::configDir(), this);
    m_settingsApi->setDatabase(m_databaseManager);
    // v1.7 (S9): the shared settings impl publishes
    // pet.panel.settings_changed onto the shared bus after every
    // successful persist (the same setEventPublisher discipline the
    // sessions use; m_eventApi is registered before this tree's
    // services — under the Qt ≥ 6.6 forward cascade it dies BEFORE the
    // settings impl, which is harmless: ~SettingsApiImpl never calls
    // the publisher).
    m_settingsApi->setEventPublisher(m_eventApi);
    m_voicePackApi = new core::VoicePackApiImpl(/*refresh=*/{}, this);
    // S7 (v1.4): shared monitor read family — wraps the sessions'
    // MonitorDataModel rings (registered after m_voicePackApi → dies
    // after it, Qt ≥ 6.6 registration order; the InstanceManager dies
    // even earlier, so the held roster pointer is dangling by the time
    // ~MonitorApiImpl runs — never called there, safe).
    m_monitorApi = new core::MonitorApiImpl(m_instanceManager, this);

    // ── Shared layout family (v1.8) ────────────────────────────────────
    // ONE LayoutApiImpl for the whole panel: config-snapshot getLayout +
    // the resetLayout / setAutoStart forwards over the same
    // InstanceSession surfaces the detail page used to call directly
    // (host UI and plugins go through the same vtable). Registered
    // after m_monitorApi → dies AFTER it (Qt ≥ 6.6 registration order);
    // no destructor cross-calls — safe.
    m_layoutApi = new core::LayoutApiImpl(m_instanceManager, this);

    // ── Rule-engine host service (S8e) ────────────────────────────────────
    // The RuleEngine core's bus/db wiring. Constructed after every API it
    // consumes (eventApi/instanceApi/tuningApi + the shared DatabaseManager
    // for the panel "rules" kv + instance blobs) and before the plugin
    // host / auto-start — so startup rules are live before any renderer
    // connects. The play_pack_audio renderer channel is wired immediately
    // (roster + WsServer both exist here); the notify_bubble sink is
    // late-injected by PanelUiBoot (NotificationStreamController is a
    // UiBoot child constructed later) — the service tolerates the null
    // sink with a per-action WARN until then. Registered after
    // m_monitorApi → dies AFTER it in the default cascade (Qt ≥ 6.6
    // registration order); its dtor calls the earlier-registered
    // instanceApi/eventApi (freed first under the forward order — the
    // exit-path use-after-free), so it is deleted explicitly in
    // ~PanelApplication() while they are still alive.
    m_ruleEngineService = new core::RuleEngineService(
        m_eventApi, m_instanceApi, m_tuningApi, m_databaseManager, this);
    m_ruleEngineService->setPackAudioChannel(
        [this](const QString& uuid, const QJsonObject& command) {
            // Locate the session by uuid over the roster's PUBLIC read
            // surface (same scan shape TuningApiImpl::sessionForUuid
            // uses) and push the serialized play_audio envelope over the
            // per-instance WS channel (MBE direct-protocol precedent).
            for (int row = 0; row < m_instanceManager->rowCount(); ++row) {
                InstanceSession* s = m_instanceManager->instanceAt(row);
                if (s != nullptr && s->uuid() == uuid) {
                    const QByteArray json = QJsonDocument(command).toJson(
                        QJsonDocument::Compact);
                    return m_wsServer->sendText(
                               s->instanceId(),
                               QString::fromUtf8(json))
                        ? 0
                        : -1;
                }
            }
            return -1;
        });

    // ── Shared rules API + owner gate wiring (S8f, v1.6) ──────────────
    // ONE RuleApiImpl over the RuleEngineService: injected into every
    // plugin context below (queryApi("pet.rules") → per-plugin
    // owner-stamping proxy). Registered after m_ruleEngineService →
    // dies AFTER it in the default cascade (Qt ≥ 6.6 registration order
    // — the old registration-reverse claim had the direction backwards;
    // the borrowed service pointer would dangle). Covered by
    // ~PanelApplication(), which deletes m_ruleApi BEFORE the service.
    m_ruleApi = new core::RuleApiImpl(m_ruleEngineService, this);
    // Owner action gate (play_motion/set_expression from plugin-owned
    // rules), checked LIVE at emission time inside the service. The
    // service passes the rule's ownerUuid — the owner plugin's OWN id,
    // stamped at registration — so the oracle does an EXACT registry
    // entry() lookup. NO prefix matching: nested plugin ids ("org.a" vs
    // "org.a.b") must never cross-match grants (audit P1 — the old
    // orderedEntries()+startsWith scan answered org.a's capability for
    // an org.a.b-owned rule = false authorization). Unknown owner id →
    // entry() == nullptr → fail closed. The write-enabled provider
    // mirrors the setWriteEnabledProvider kv read below (absent/"1" =
    // enabled — the documented default; LIVE read so flipping the row
    // stops already-registered plugin rules without a restart).
    m_ruleEngineService->setOwnerCapabilityQuery(
        [this](const QString& ownerPluginId,
               QLatin1StringView capability) {
            const core::PluginEntry* entry =
                m_pluginRegistry.entry(ownerPluginId);
            return entry != nullptr
                && entry->manifest.capabilities.contains(capability);
        });
    m_ruleEngineService->setOwnerWriteEnabled([this]() {
        return m_databaseManager->getValue(
                   QStringLiteral("plugin_write_enabled"),
                   QStringLiteral("1"))
            != QStringLiteral("0");
    });

    // ── Plugin framework (P4, §B.6) ─────────────────────────────────────
    // Registry first (pure store, no deps), host last (observes the roster
    // above; its page model / stream / config root are mounted later by
    // PanelUiBoot, before main() calls initializeAll()). Both parented to
    // this tree — registered after the five services → destroyed AFTER
    // them (Qt ≥ 6.6 registration order — the old registration-reverse
    // claim had the direction backwards). Plugin shutdown still happens
    // while the roster/network/database are alive: main() calls
    // pluginHost()->shutdownAll() explicitly BEFORE any teardown;
    // ~PluginHost itself makes no cross-calls.
    // P5: the download service BEFORE the host (contexts receive it at
    // initializeAll). Registered between registry/host — under the
    // forward cascade the service dies before the host; harmless:
    // plugin shutdown runs via main()'s shutdownAll() while both are
    // alive, and neither destructor reaches the other. Destructor
    // silently cancels active jobs — the exit path never blocks.
    // Storage-layout revision: staging + installs live under the per-user
    // DATA dir (<dataDir>/downloads/ → <dataDir>/VoicePacks/), never inside
    // the (possibly read-only) install tree; DownloadService creates both.
    m_downloadService = new core::DownloadService(
        ConfigDir::downloadsDir(), ConfigDir::userVoicePacksDir(), this);
    m_pluginHost = new core::PluginHost(m_pluginRegistry, this);
    m_pluginHost->setInstanceManager(m_instanceManager);
    m_pluginHost->setInstanceApi(m_instanceApi);
    m_pluginHost->setInstanceControlApi(m_instanceControlApi);
    // v1.3 families: shared impls injected; contexts gate tuning/settings
    // per manifest capability + the live write switch.
    m_pluginHost->setTuningApi(m_tuningApi);
    m_pluginHost->setModelApi(m_modelApi);
    m_pluginHost->setSettingsApi(m_settingsApi);
    m_pluginHost->setVoicePackApi(m_voicePackApi);
    m_pluginHost->setMonitorApi(m_monitorApi); // S7 (v1.4)
    m_pluginHost->setLayoutApi(m_layoutApi);   // v1.8
    m_pluginHost->setEventApi(m_eventApi);     // S8a (v1.5)
    m_pluginHost->setRuleApi(m_ruleApi);       // S8f (v1.6)
    // S2: the host-global plugin_write_enabled kill-switch (panel_config
    // kv, absent/"1" = enabled — the documented default). LIVE read on
    // every queryApi call, so flipping the kv row revokes plugin write
    // access without a panel restart. getValue never throws (failed db →
    // fallback value → enabled), keeping the GUI-thread no-block contract.
    m_pluginHost->setWriteEnabledProvider([this]() {
        return m_databaseManager->getValue(
                   QStringLiteral("plugin_write_enabled"),
                   QStringLiteral("1"))
            != QStringLiteral("0");
    });
    m_pluginHost->setConfigRoot(ConfigDir::configDir());
    m_pluginHost->setDownloadService(m_downloadService);
}

PanelApplication::~PanelApplication()
{
    // Exit-path use-after-free fix (2026-10 teardown diagnosis, ASAN
    // 100%-repro → 10/10 green with this dtor). Qt >= 6.6 deletes QObject
    // children in REGISTRATION order (first registered dies FIRST — the
    // tree's old comments had the Qt 6.6 change direction backwards). Under
    // that default cascade EventApiImpl (#early) and InstanceApiImpl die
    // BEFORE the destructors that still call into them:
    //   ~RuleEngineService → m_instanceApi->unsubscribeInstances(this)
    //   ~InstanceSession   → m_eventPublisher->unsubscribe(&m_mbeAdapter)
    // i.e. heap-use-after-free at exit (release flakiness: glibc tcache
    // clobbers the freed vptr slot → sporadic SEGV/heap-corruption abort).
    // Tear the cross-referencing services down HERE — the dtor body runs
    // while every child is still alive, and deleting a child removes it
    // from the QObject children list, so the default cascade afterwards
    // cannot double-delete.
    delete m_ruleApi;           // borrows m_ruleEngineService
    m_ruleApi = nullptr;
    delete m_ruleEngineService; // unsubscribes instanceApi + eventApi (both alive here)
    m_ruleEngineService = nullptr;
    delete m_instanceManager;   // qDeleteAll sessions unsubscribe via eventApi (alive here)
    m_instanceManager = nullptr;
}

DatabaseManager& PanelApplication::databaseManager()
{
    return *m_databaseManager;
}

PanelStateManager& PanelApplication::panelStateManager()
{
    return *m_panelStateManager;
}

WsServer* PanelApplication::wsServer()
{
    return m_wsServer;
}

PendingRequests* PanelApplication::pendingRequests()
{
    return m_pendingRequests;
}

InstanceManager* PanelApplication::instanceManager()
{
    return m_instanceManager;
}

core::InstanceApiImpl* PanelApplication::instanceApi()
{
    return m_instanceApi;
}

core::InstanceControlApiImpl* PanelApplication::instanceControlApi()
{
    return m_instanceControlApi;
}

core::TuningApiImpl* PanelApplication::tuningApi()
{
    return m_tuningApi;
}

core::ModelApiImpl* PanelApplication::modelApi()
{
    return m_modelApi;
}

core::SettingsApiImpl* PanelApplication::settingsApi()
{
    return m_settingsApi;
}

core::VoicePackApiImpl* PanelApplication::voicePackApi()
{
    return m_voicePackApi;
}

core::MonitorApiImpl* PanelApplication::monitorApi()
{
    return m_monitorApi;
}

core::LayoutApiImpl* PanelApplication::layoutApi()
{
    return m_layoutApi;
}

core::EventBus* PanelApplication::eventBus()
{
    return m_eventBus;
}

core::EventApiImpl* PanelApplication::eventApi()
{
    return m_eventApi;
}

core::RuleEngineService* PanelApplication::ruleEngineService()
{
    return m_ruleEngineService;
}

core::RuleApiImpl* PanelApplication::ruleApi()
{
    return m_ruleApi;
}

core::PluginRegistry& PanelApplication::pluginRegistry()
{
    return m_pluginRegistry;
}

core::PluginHost* PanelApplication::pluginHost()
{
    return m_pluginHost;
}

core::DownloadService* PanelApplication::downloadService()
{
    return m_downloadService;
}

// Moved from main() (P3/M4). Construction + wiring statements are verbatim;
// only the storage changed (main stack objects → heap children of this
// PanelApplication). Registered AFTER the five services → destroyed LAST
// among this tree's children (Qt ≥ 6.6 registration order — the §B.2 v2
// annotation's registration-reverse claim had the direction backwards, so
// this is NOT the old stack order where salvo/PM died first). Harmless:
// neither destructor touches the services.
void PanelApplication::wireLegacyInstanceZeroSenders()
{
    // StartupSalvo + ProcessManager: Phase-5 single-instance holders wired
    // here for sender injection; todo 2 (InstanceSession) owns them
    // per-instance. These globals are now redundant (InstanceSession owns
    // its own), but they are harmless and NetworkWiringTest asserts the
    // sender wiring, so they stay until a dedicated cleanup task. (M2 note:
    // they used to be constructed between PendingRequests and the roster;
    // with the service tree in one ctor they now construct after it — inert
    // reorder, nothing between the old points observes them and no renderer
    // connects until the event loop runs.)
    m_startupSalvo = new StartupSalvo(this);
    m_processManager = new ProcessManager(this);

    // Wire sender-injection seams → WsServer::sendText(instanceId, ...). These
    // legacy globals always targeted instance_id 0 (Phase 0-4 single-instance
    // assumption); InstanceSession owns its own per-instance senders below.
    m_startupSalvo->setCommandSender([this](const QString& json) {
        m_wsServer->sendText(0, json);
    });
    m_processManager->setShutdownSender([this]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        m_wsServer->sendText(0, QString::fromUtf8(json));
    });
}

// Moved from main() (P3/M4) verbatim.
void PanelApplication::launchAutoStartInstances()
{
    // Auto-start (per-instance config.autoStart): launch every instance whose
    // flag is set once the QML UI is up — the renderer windows appear
    // alongside the panel. Queued via QTimer::singleShot(0) so the first
    // frame paints before the (blocking, process-spawning) start() calls run.
    QTimer::singleShot(0, [this]() {
        for (int i = 0; i < m_instanceManager->rowCount(); ++i) {
            InstanceSession* s = m_instanceManager->instanceAt(i);
            if (s != nullptr && s->autoStartEnabled()) {
                LOG_INFO("autoStart: launching instance \"{}\"",
                         s->label().toStdString());
                // Audit follow-up (last host lifecycle dual-track): route the
                // write through the SHARED pet::IInstanceControlApi facade —
                // the same vtable RosterApiModel/InstanceControlBridge and
                // every plugin use — instead of a direct session->start().
                // m_instanceControlApi is constructed in this class's ctor
                // (before any caller can reach this method); the facade
                // resolves the session by uuid (roster lookup cannot fail
                // here — the pointer came off the same roster — and no
                // delete can be pending before the event loop runs, so a
                // non-Ok result is defensive-only: WARN and continue with
                // the remaining instances, never crash / never abort).
                const pet::PluginError err =
                    m_instanceControlApi->start(s->uuid());
                if (err != pet::PluginError::Ok) {
                    LOG_WARN("autoStart: instanceControlApi().start('{}') "
                             "rejected (PluginError {}), instance \"{}\" "
                             "not launched",
                             s->uuid().toStdString(),
                             static_cast<unsigned>(err),
                             s->label().toStdString());
                }
            }
        }
    });
}
