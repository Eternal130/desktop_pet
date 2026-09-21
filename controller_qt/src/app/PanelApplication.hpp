#pragma once

#include <QObject>

#include "core/PanelConfig.hpp"
#include "core/PluginRegistry.hpp"

class DatabaseManager;
class PanelStateManager;
class WsServer;
class PendingRequests;
class InstanceManager;
class StartupSalvo;
class ProcessManager;

namespace core {
class DownloadService;
class InstanceApiImpl;
class InstanceControlApiImpl;
class ModelApiImpl;
class SettingsApiImpl;
class TuningApiImpl;
class VoicePackApiImpl;
class PluginHost;
}

// PanelApplication — the application service tree carved out of main()
// (P3/M2, docs/refactor §B.2): DatabaseManager, PanelStateManager, WsServer,
// PendingRequests, InstanceManager plus all wiring between them, constructed
// and connected in main()'s original declaration/connect order.
//
// Lifecycle: a QObject parent tree REPLACES the old stack-reverse discipline
// for these five — each service is heap-allocated with `this` as its Qt
// parent. main() declares the PanelApplication before every UI bridge and
// the engine, so by stack-reverse the whole tree is destroyed after the QML
// context ("services outlive QML" invariant preserved at the main() level).
// Children are registered in the old declaration order; Qt destroys children
// registration-reverse — Qt 6.6+ implementation behavior, NOT a documented
// contract (§B.2 v2 annotation) — which reproduces the old stack teardown
// sequence inside the tree. Details at the registration sites in the .cpp.
//
// No Q_OBJECT: the class adds no signals/slots of its own; it is a pure
// composition + ownership scope (AUTOMOC therefore not required).
class PanelApplication : public QObject
{
public:
    explicit PanelApplication(QObject* parent = nullptr);

    // Ownership seam for main() (context-property registration stays there
    // until M3). The service objects are parented to this PanelApplication
    // and die with it — callers must not delete them.
    DatabaseManager& databaseManager();
    PanelStateManager& panelStateManager();
    WsServer* wsServer();
    PendingRequests* pendingRequests();
    InstanceManager* instanceManager();

    // S3 roster dogfooding: THE shared pet::IInstanceApi implementation —
    // constructed here (right after the roster it observes) and injected
    // both into every plugin context (via PluginHost) and into the panel's
    // own RosterApiModel (via PanelUiBoot). Host UI and plugins therefore
    // go through the same object and the same vtable; dies before the
    // InstanceManager (registration-reverse teardown).
    core::InstanceApiImpl* instanceApi();

    // S2 (v1.2): THE shared pet::IInstanceControlApi implementation —
    // same construction/sharing pattern as instanceApi(): served to
    // plugins through PluginHost → queryApi("pet.instance_control")
    // (capability + plugin_write_enabled gated at the context) and to the
    // panel's own RosterApiModel write bridge (NOT gated — the host UI is
    // the host). Registered after m_instanceApi → dies before it and
    // before the InstanceManager (registration-reverse teardown).
    core::InstanceControlApiImpl* instanceControlApi();

    // S5 (v1.3): THE shared pet::ITuningApi implementation — same
    // construction/sharing pattern as instanceControlApi(): served to
    // plugins through PluginHost → queryApi("pet.instance_tuning")
    // (capability + plugin_write_enabled gated at the context) and to
    // the panel's own InstanceControlBridge + VoicePackController mount
    // path (NOT gated — the host UI is the host). Registered after
    // m_instanceControlApi → dies before it (registration-reverse).
    core::TuningApiImpl* tuningApi();

    // S5 (v1.3): THE shared pet::IModelApi implementation — ONE model
    // scan cache serving the plugin family (queryApi("pet.model"), a
    // read → ungated) AND the panel's own ModelController (the library
    // page consumes this same object). Same lifetime discipline as
    // tuningApi(). The renderer dir is injected later by PanelUiBoot
    // (setRendererDir, mirroring the ModelController seam).
    core::ModelApiImpl* modelApi();

    // S6 (v1.3): THE shared pet::ISettingsApi implementation — the SAME
    // load-modify-save persistence path the panel's own
    // PanelConfigController writes land on (it forwards here since S6).
    // Served to plugins through queryApi("pet.settings")
    // (settings_write + switch gated at the context). Same lifetime
    // discipline as tuningApi().
    core::SettingsApiImpl* settingsApi();

    // S6 (v1.3): THE shared voice-pack read API (core::VoicePackApiImpl
    // — the pet::IVoicePackApi handed to every plugin context). Its pack
    // source + refresh seams are wired by PanelUiBoot onto
    // VoicePackController's single scan; packsChanged fanout reaches the
    // IPackListObserver family. Same lifetime discipline as tuningApi().
    core::VoicePackApiImpl* voicePackApi();

    // Plugin framework (P4, §B.6): the registry (entry store + state
    // machine) is created FIRST in the ctor; the host (initialize/shutdown
    // driver) LAST, so it can observe every earlier service. main()
    // registers static-plugin factories into the registry between
    // construction and PanelUiBoot; the host's initializeAll() runs after
    // the UI boot mounted the page model/manager (see main.cpp).
    core::PluginRegistry& pluginRegistry();
    core::PluginHost* pluginHost();

    // P5 (§B.4): the process-wide download chokepoint — constructed in the
    // service tree BEFORE the plugin host (which hands it to plugin
    // contexts, capability-gated). Staging <dataDir>/downloads/, install
    // root <dataDir>/VoicePacks/ (per-user, never the install tree). Its
    // destructor silently cancels active jobs — the exit path never blocks.
    core::DownloadService* downloadService();

    // Snapshot loaded once at construction (the old `const PanelConfig
    // panelCfg = psm.load();` local in main), consumed for the initial*
    // context properties.
    const PanelConfig& panelConfig() const { return m_panelConfig; }

    // Legacy Phase-5 instance-0 holders (StartupSalvo + ProcessManager) and
    // their sender-injection wiring, moved from main() (M4). Call exactly
    // once, right after construction — same position the old main() stack
    // block sat in relative to the five services.
    void wireLegacyInstanceZeroSenders();

    // Auto-start salvo (per-instance config.autoStart), moved from main()
    // (M4). Registers a singleShot(0) — call after engine.loadFromModule so
    // the first frame paints before the (blocking, process-spawning) start()
    // calls run, exactly like the old inline block.
    void launchAutoStartInstances();

private:
    // Registration order = old main() declaration order; destruction runs
    // this list in reverse (see class comment for the assumption caveat).
    DatabaseManager* m_databaseManager;
    PanelStateManager* m_panelStateManager;
    WsServer* m_wsServer;
    PendingRequests* m_pendingRequests;
    InstanceManager* m_instanceManager;
    // S3: shared roster API (see instanceApi()); registered after the
    // manager → destroyed before it (registration-reverse).
    core::InstanceApiImpl* m_instanceApi = nullptr;
    // S2 (v1.2): shared instance-control write API (see
    // instanceControlApi()); registered after m_instanceApi → destroyed
    // before it (registration-reverse).
    core::InstanceControlApiImpl* m_instanceControlApi = nullptr;
    // S5/S6 (v1.3): shared v1.3-family implementations (see the accessor
    // comments); registered after m_instanceControlApi, in this order →
    // destroyed before it and each other, registration-reverse.
    core::TuningApiImpl* m_tuningApi = nullptr;
    core::ModelApiImpl* m_modelApi = nullptr;
    core::SettingsApiImpl* m_settingsApi = nullptr;
    core::VoicePackApiImpl* m_voicePackApi = nullptr;
    // Registered last (wireLegacyInstanceZeroSenders) → destroyed first.
    StartupSalvo* m_startupSalvo = nullptr;
    ProcessManager* m_processManager = nullptr;
    PanelConfig m_panelConfig;

    // Plugin framework (P4): registry (pure logic class, held by value,
    // created before every consumer) + host (QObject child, created after
    // every service — it dereferences instanceManager at boot). The host
    // is registered LAST in the ctor → destroyed FIRST among this tree's
    // plugin objects — plugin teardown precedes roster/network teardown.
    core::PluginRegistry m_pluginRegistry;
    core::PluginHost* m_pluginHost = nullptr;
    core::DownloadService* m_downloadService = nullptr;
};
