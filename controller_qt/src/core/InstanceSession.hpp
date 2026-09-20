#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <optional>

#include "core/HitAreaCacheManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InteractionHandler.hpp"
#include "core/ModelInfoParser.hpp"
#include "core/MountedBehaviorEngine.hpp"
#include "core/ProcessManager.hpp"
#include "core/RestartController.hpp"
#include "core/Scheduler.hpp"
#include "core/StartupSalvo.hpp"
#include "network/Envelope.hpp"
#include "network/EventRegistry.hpp"
#include "network/MessageDispatcher.hpp"
#include "system/ResourceStatsCollector.hpp"
#include "ui/MonitorDataModel.hpp"

class WsServer;
class PendingRequests;

// Dialogue sink seam type (bubble-stream switch). Declared OUTSIDE the
// Q_OBJECT class so moc never parses the raw std::function signature (moc's
// parser mis-tokenizes multi-const-ref std::function parameters).
// Params: (instanceId, name, avatar, text, durationMs) — durationMs 0 lets
// the stream apply its default; callers with a known audio length pass
// audioMs + 8s display margin.
using DialogueSinkFn = std::function<void(const QString&, const QString&,
                                             const QString&, const QString&,
                                             int)>;

// InstanceSession (Phase 5, todo 2) — the per-instance orchestrator. Owns the
// COMPLETE runtime tuple for ONE pet instance: its ProcessManager, its own
// MessageDispatcher + EventRegistry (M2 resolution — NOT shared from main.cpp),
// StartupSalvo, Scheduler (todo 8), InteractionHandler (todo 9),
// HitAreaCacheManager + InstanceConfigManager (todo 10), plus live state
// (status, connected, modelLoaded, counters, flags).
//
// Only PendingRequests is injected by reference — it is the GLOBAL id→result
// table (M2: response-id matching must work across all instances, so the table
// is shared). Every InstanceSession = one pet = one dispatcher + one registry.
//
// Lifecycle:
//   start()   resolve renderer path → register token on WsServer → launch the
//             renderer via ProcessManager → register EventRegistry defaults +
//             the `ready` + `model_loaded` + `hit` + `drag_end` +
//             `motion_finished` overrides → wait for the WS connect.
//             On path-miss or launch failure → startFailed(reason), status=error.
//   ready     → fire StartupSalvo::sendSalvo (the 9-command bootstrap salvo).
//   model_loaded → parse the .model3.json via ModelInfoParser → consult the
//             HitAreaCacheManager for cached hitAreas → send set_hit_areas →
//             store ModelInfo → start the Scheduler (idle-motion cycling) →
//             setModelLoaded(true).
//   hit       → MountedBehaviorEngine (Phase 8 todo 20) routes to play_motion_ext
//             when a voice-pack group exists for the hit area; otherwise
//             InteractionHandler handles it (3-tier case-fold → play_motion).
//             Voice-pack path pauses the idle Scheduler.
//   drag_end  → extract payload.window_x/window_y → persist via
//             InstanceConfigManager::save (blueprint §4.4.3 drag persistence).
//   motion_finished → judge idle vs non-idle (variant A: group=="Idle";
//             variant B: motion_path present). Non-idle + connected →
//             Scheduler::triggerNow (interrupt idle cycle). Idle → no-op
//             (avoids feedback loop).
//   stop()    set manuallyStopping=true (suppresses crash handling) →
//             ProcessManager::stop() 3-stage graceful shutdown → clear flags.
//   restart() stop() + reset flag + start(). restartAttempts++ (todo 12 wires
//             RestartController policy around this counter).
//   onMessage(env) the inbound-router entry point. WsServer / InstanceManager
//             (todo 3/11) calls this → m_dispatcher.dispatch(env).
//
// QML binding surface: 10 Q_PROPERTYs (label, modelName, status, connected,
// modelLoaded, opacity, targetFps, volume, muted, instanceId). The full
// InstanceConfig (28 fields) is also reachable via config() for pages that need
// fields not yet exposed as individual properties.
//
// configBasePath injection (todo 10): the InstanceConfigManager +
// HitAreaCacheManager both write under this root. Tests inject a QTemporaryDir
// path; production leaves it empty → real ConfigDir::configDir(). This lets
// drag_end persistence + hitArea caching be tested without touching the user's
// real ~/.config/desktop-pet/.
//
// todo 20: MountedBehaviorEngine enriches the hit handler with voice-pack
// play_motion_ext routing when a voice pack is mounted (todo 21 wires the
// mount UI to populate m_behaviorEngine with a parsed VoicePackInfo).
class InstanceSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString label       READ label       CONSTANT)
    Q_PROPERTY(QString avatar      READ avatar      CONSTANT)
    Q_PROPERTY(QString uuid        READ uuid        CONSTANT)
    Q_PROPERTY(QString modelName   READ modelName   NOTIFY modelNameChanged)
    Q_PROPERTY(QString status      READ status      NOTIFY statusChanged)
    Q_PROPERTY(bool     connected  READ connected   NOTIFY connectedChanged)
    Q_PROPERTY(bool     modelLoaded READ modelLoaded NOTIFY modelLoadedChanged)
    Q_PROPERTY(double   opacity    READ opacity     WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(int      targetFps  READ targetFps   WRITE setFps     NOTIFY targetFpsChanged)
    Q_PROPERTY(double   volume     READ volume      WRITE setVolume  NOTIFY volumeChanged)
    Q_PROPERTY(bool     muted      READ muted       WRITE setMuted   NOTIFY mutedChanged)
    Q_PROPERTY(int      instanceId READ instanceId  CONSTANT)
    // Model-switch failure observability (模型库 B 档): the renderer's
    // model_load_failed event bumps the revision so QML bindings that call
    // Q_INVOKABLE accessors re-evaluate, and lastModelLoadError carries the
    // renderer's error_message (empty string before any failure).
    Q_PROPERTY(int      modelLoadFailureRevision READ modelLoadFailureRevision NOTIFY modelLoadFailed)
    Q_PROPERTY(QString  lastModelLoadError       READ lastModelLoadError       NOTIFY modelLoadFailed)

public:
    // Construct a session bound to a shared WsServer (token registry + send
    // channel) and the GLOBAL PendingRequests table (injected by reference —
    // M2: only network object shared across instances). The config is copied
    // in (InstanceSession owns its config snapshot; re-persist via
    // InstanceConfigManager when a setter mutates it).
    //
    // configBasePath (todo 10): the config-dir root for the owned
    // InstanceConfigManager + HitAreaCacheManager. Empty (default) → real
    // ConfigDir::configDir(). Tests inject a QTemporaryDir path so drag_end
    // persistence + hitArea caching can be verified without touching the user's
    // real ~/.config/desktop-pet/.
    InstanceSession(InstanceConfig config, WsServer& server,
                    PendingRequests& pending,
                    const QString& configBasePath = {},
                    QObject* parent = nullptr);
    ~InstanceSession() override;

    // ── Read accessors (back the Q_PROPERTYs) ───────────────────────────────
    QString label() const;
    QString avatar() const;
    QString uuid() const;
    QString modelName() const;
    QString status() const;
    bool    connected() const;
    bool    modelLoaded() const;
    double  opacity() const;
    int     targetFps() const;
    double  volume() const;
    bool    muted() const;
    int     instanceId() const;
    int     modelLoadFailureRevision() const { return m_modelLoadFailureRevision; }
    QString lastModelLoadError() const { return m_lastModelLoadError; }

    // The full config snapshot (28 fields). Mutable setters below persist the
    // change into m_config so the next read reflects it.
    const InstanceConfig& config() const;

    // Parsed model metadata from the .model3.json (populated on model_loaded).
    // std::nullopt before model_loaded fires OR if the .model3.json is
    // missing/corrupt. Todo 6 (UI) reads motionGroups/expressions; todo 8
    // (Scheduler) reads motionGroups for idle cycling.
    std::optional<core::ModelInfo> modelInfo() const;

public slots:
    // Lifecycle (slots so QML / InstanceManager can invoke them).
    void start();
    void stop();
    void restart();
    void loadModel(const QString& modelName);

    // Inbound-router entry point. WsServer::messageReceived → this → dispatch.
    // Todo 3/11 routes by instance_id; for Phase 5 (single instance) the one
    // session receives every envelope.
    void onMessage(const Envelope& env);

    // ── Q_INVOKABLE thin setters (send a command + persist to m_config) ──────
    // Each builds the command via the Protocol factories and pushes it through
    // WsServer::sendText. Mirrors Java's InstanceSession wrappers.
    Q_INVOKABLE void setOpacity(double opacity);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setMuted(bool muted);
    Q_INVOKABLE void setFps(int fps);

    // ── Phase-5 todo 6 (UI helpers) ──────────────────────────────────────────
    // Q_INVOKABLE model-metadata accessors + runtime command triggers used by
    // InstanceDetailPage.qml. The metadata accessors wrap m_modelInfo + the
    // free function core::scanAvailableModels so QML never touches std::
    // optional<ModelInfo> or QDir directly. The triggers build play_motion /
    // set_expression envelopes inline (Protocol factories arrive in todo 16)
    // and route them through sendCommand, mirroring the setters.
    //
    // Available model directories under the renderer's Resources/Models/ tree.
    // Empty when no renderer dir is known yet (pre-start) or none bundled.
    Q_INVOKABLE QStringList availableModels() const;

    // Motion group names from the parsed .model3.json (keys of motionGroups).
    // Empty until model_loaded fires; the UI binds visibility to modelLoaded
    // so the grid repopulates on load.
    Q_INVOKABLE QStringList motionGroupNames() const;

    // Motion count for one group (0 if the group is absent or no model loaded).
    Q_INVOKABLE int motionCount(const QString& group) const;

    // Expression names from the parsed .model3.json (empty when none / unloaded).
    Q_INVOKABLE QStringList expressionNames() const;

    // Hit-area names from the parsed .model3.json HitAreas[].Name (empty
    // until model_loaded). Design-doc §② third stage tab.
    Q_INVOKABLE QStringList hitAreaNames() const;

    // Live crash-recovery attempt count (0..RestartController::kMaxAttempts,
    // reset on successful reconnect). Design-doc §③ crash-recovery card.
    Q_INVOKABLE int restartAttempts() const;

    // Build + send a play_motion command (interface.md §B.1). priority defaults
    // to PriorityNormal (2). The renderer silently ignores unknown group/index.
    Q_INVOKABLE void playMotion(const QString& group, int index);

    // Manually trigger a hit area as if the renderer had reported a `hit`
    // event for it — runs the SAME decision chain (MountedBehaviorEngine →
    // play_motion_ext when a pack is mounted, else InteractionHandler →
    // play_motion). This is what the hit-area chips and voice-pack preview
    // chips call; playMotion(group) is wrong for them because a hit-area
    // name is not a motion group name.
    Q_INVOKABLE void triggerHitArea(const QString& areaId);

    // Build + send a set_expression command (interface.md §B.4). The renderer
    // silently ignores an unknown expression_id.
    Q_INVOKABLE void setExpression(const QString& expressionId);

    // ── Voice-pack mount (todo 21) ─────────────────────────────────────────
    // Mount the voice pack at `packPath` (the absolute pack directory that
    // contains meta.mko): parse + populate m_behaviorEngine + persist the
    // choice to InstanceConfig.voicePack. Empty/unparseable pack → unmount.
    // Returns true when the engine ended up enabled.
    Q_INVOKABLE bool mountVoicePack(const QString& packPath);

    // Unmount the voice pack (engine sentinel, hit falls back to
    // InteractionHandler) and persist voicePack = "".
    Q_INVOKABLE void unmountVoicePack();

    // The mounted pack's absolute directory (empty = not mounted).
    Q_INVOKABLE QString mountedVoicePack() const;

    // ── Dialogue sink seam (bubble-stream switch) ──────────────────────────
    // Injected by InstanceManager (mirrors the setAssetRefDetacher pattern).
    // The sink receives (instanceId-as-string, label, avatar, text) when a
    // mounted voice-pack behavior fires with dialogue text. Empty default =
    // no-op (tests without the stream stay clean).
    void setDialogueSink(DialogueSinkFn sink)
    { m_dialogueSink = std::move(sink); }

    // ── Phase-5 Wave 8 todo 22 (layout UI helpers) ───────────────────────────
    // Q_INVOKABLE triggers consumed by InstanceDetailPage.qml's Layout panel.
    // getLayout sends §E.2 get_layout (empty payload); the response routes back
    // via the layout_state EVENT by action (interface.md §7.3 pseudo-response
    // rule), NOT via a pending request — so sendCommand is used directly and
    // NO entry is registered in PendingRequests. The layout_state handler syncs
    // m_config + emits layoutUpdated so the UI can refresh sliders.
    // resetLayout sends §E.3 reset_layout (fire-and-forget): the renderer
    // applies defaults; the resulting layout_changed (if emitted) is persisted
    // by handleLayoutChangedEvent. resetLayout does NOT block on the Response.
    Q_INVOKABLE void getLayout();
    Q_INVOKABLE void resetLayout();

    // Read-only InstanceConfig field accessors for UI display. These three are
    // not (yet) backed by Q_PROPERTY NOTIFY because the controller-side persist
    // path is todo 7+: the parameter panel renders them display-only with a
    // note. Exposing them as Q_INVOKABLE lets QML read the live config snapshot
    // without touching the C++ InstanceConfig type.
    Q_INVOKABLE QString dragMode() const;         // "direct" | "physics"
    Q_INVOKABLE int idleIntervalSeconds() const;  // 1..60
    Q_INVOKABLE bool autoStartEnabled() const;    // start-with-panel flag
    Q_INVOKABLE void setAutoStart(bool enabled);  // persist + no command (renderer not up yet at toggle time)

    // ── Phase-5 Wave 8 todo 18 (monitor UI helper) ───────────────────────────
    // Returns the per-session MonitorDataModel as a QObject* so QML can bind
    // its Q_INVOKABLE accessors (latestRendererCpuPercent / isStaleNow /
    // controllerCpuSeries / etc.) directly. MonitorPage.qml calls
    // `instance.monitorModel()` once on instance swap, then connects to its
    // snapshotAppended signal to refresh the 6 charts + stale banner. The
    // model is owned by InstanceSession (m_monitorModel member below); the
    // page does NOT take ownership.
    Q_INVOKABLE QObject* monitorModel();

signals:
    void statusChanged();
    void connectedChanged();
    void modelLoadedChanged();
    void modelNameChanged();
    void opacityChanged();
    void targetFpsChanged();
    void volumeChanged();
    void mutedChanged();

    // model_load_failed (interface.md §B): the renderer rejected a load_model
    // (missing model dir, corrupt .model3.json, ...). Carries the error
    // message; also drives the modelLoadFailureRevision + lastModelLoadError
    // Q_PROPERTY NOTIFYs.
    void modelLoadFailed(const QString& error);

    // Emitted when start() fails (renderer missing / launch error) OR when the
    // renderer exits unexpectedly (crash, !manuallyStopping). QML shows the
    // reason; stop()-initiated exits never emit this (m_manuallyStopping=true).
    void startFailed(const QString& reason);

    // Emitted for every outbound command envelope (salvo commands, thin-setter
    // commands, set_hit_areas, play_motion from hit + idle scheduler, shutdown).
    // `action` is the envelope action (e.g. "load_model", "set_opacity"). Tests
    // capture the stream via QSignalSpy to assert the salvo order + set_hit_areas
    // delivery + play_motion on hit; QML can observe it for a command-log panel.
    void commandSent(const QString& action);

    // Emitted when the Scheduler fires an idle motion (todo 10). Re-emits
    // Scheduler::triggered so tests + QML can observe scheduler activity from a
    // single InstanceSession signal without reaching into the private m_scheduler
    // member. The group is the configured idle group (default "Idle"); index is
    // the randomized 0..count-1 selected by the scheduler.
    void idleMotionTriggered(const QString& group, int index);

    // Emitted when a layout_state event arrives (todo 22) — the response to
    // get_layout, routed by ACTION per interface.md §7.3. Carries the freshly
    // synced offset_x/offset_y/scale so the UI (InstanceDetailPage Layout panel)
    // can refresh any bound sliders. Tests spy on this to prove the
    // pseudo-response path routes by action, not by id.
    void layoutUpdated(double offsetX, double offsetY, double scale);

private:
    // Build, serialize (compact), and send a command envelope via the shared
    // WsServer. Centralizes the serialize→send pattern the setters share.
    void sendCommand(const Envelope& env);

    // setStatus / setConnected / setModelLoaded — emit the NOTIFY only when the
    // value actually changes (avoids spurious QML re-evaluation).
    void setStatus(const QString& status);
    void setConnected(bool connected);
    void setModelLoaded(bool loaded);

    // ProcessManager::exited → crash-vs-user-stop routing. When
    // m_manuallyStopping is true the exit was initiated by stop() — no
    // startFailed. Otherwise a crashed/non-zero exit → startFailed + error.
    void onProcessExited(int exitCode, bool crashed);

    // WsServer::connectionStateChanged → track connected for this instance.
    void onConnectionStateChanged(int state, int instanceId);

    // Generate a per-session auth token (32 random bytes, hex). Mirrors the
    // Java ProcessManager.generateSecureToken pattern — the token is a runtime
    // value, not persisted config.
    static QString generateToken();

    // Resolve the .model3.json path for the current model: the renderer ships
    // Resources/Models/<modelName>/<modelName>.model3.json next to its exe
    // (architecture-blueprint.md §3.4). Uses m_rendererDir (stored in start()).
    QString resolveModel3JsonPath() const;

    // ── Phase-5 todo 10 event handlers ───────────────────────────────────────
    // Bodies live in InstanceSessionHandlers.cpp (extracted to keep
    // InstanceSession.cpp under the 250 pure-LOC ceiling). Each is invoked by a
    // one-liner lambda registered via m_dispatcher.registerEventHandler in
    // start() AFTER registerDefaults() — QMap::insert replaces, so the override
    // wins over the log-only default.

    // hit → if MountedBehaviorEngine has a voice-pack group for payload.area_id,
    // build play_motion_ext (todo 20); else delegate to InteractionHandler (3-
    // tier case-fold lookup → play_motion). The voice-pack path also pauses
    // the idle Scheduler; the motion_finished handler resumes it via triggerNow.
    // When the chosen action carries dialogue text, it is forwarded to the
    // dialogue sink (bubble stream) — never to the renderer.
    void handleHitEvent(const Envelope& env);

    // drag_end → extract payload.window_x/window_y → persist m_config via
    // InstanceConfigManager::save (blueprint §4.4.3). Missing/non-integer
    // fields → no-op (never crashes, never partial-writes).
    void handleDragEndEvent(const Envelope& env);

    // motion_finished → judge idle vs non-idle (interface.md §E: variant A has
    // {group,index}, variant B has {motion_path}). Non-idle + connected →
    // Scheduler::triggerNow (interrupt idle cycle). Idle → no-op (feedback-loop
    // guard).
    void handleMotionFinishedEvent(const Envelope& env);

    // layout_changed (interface.md §I) → extract offset_x/offset_y/scale →
    // persist m_config.layoutOffsetX/Y/Scale via InstanceConfigManager (todo 22).
    // The renderer emits this on Shift+drag / Shift+scroll user layout edits;
    // the controller mirrors the runtime values into InstanceConfig so the next
    // launch restores them via set_layout in the startup salvo. Missing/non-
    // numeric fields → no-op (never partial-writes, never crashes).
    void handleLayoutChangedEvent(const Envelope& env);

    // window_resized (interface.md §J) → extract window_width/window_height/
    // window_x/window_y → persist m_config.windowWidth/Height/X/Y (todo 22).
    // The renderer emits this on Ctrl+scroll window resize (center-preserving);
    // the controller mirrors the values so the next launch restores them via
    // set_size + set_position. Missing/non-numeric fields → no-op.
    void handleWindowResizedEvent(const Envelope& env);

    // layout_state (interface.md §L) → the pseudo-response to get_layout (§E.2),
    // routed by ACTION not by id (interface.md §7.3). Extract offset_x/offset_y/
    // scale → update m_config (NO persist — this is a read-back sync, the values
    // are already on disk or will be when the user edits via layout_changed) →
    // emit layoutUpdated so the UI refreshes. Missing/non-numeric → no-op.
    void handleLayoutStateEvent(const Envelope& env);

    // model_load_failed (interface.md §B) → the renderer could not load the
    // requested model (payload: error_code, error_message). Records the
    // message + bumps the failure revision + emits modelLoadFailed so the
    // model-library / detail UI can surface WHY the switch failed. Also
    // clears modelLoaded (the previous model was already torn down
    // renderer-side when it attempts a load). Missing payload fields →
    // generic message. Never throws.
    void handleModelLoadFailedEvent(const Envelope& env);

    // ── Phase-5 Wave 8 todo 18 (monitor poller + stats_state merge) ──────────
    // Bodies live in InstanceSessionMonitor.cpp (extracted to keep
    // InstanceSession.cpp under the 250 pure-LOC ceiling, same split as
    // InstanceSessionHandlers.cpp). Each is invoked from a one-liner lambda
    // registered in InstanceSession::start().

    // 2s QTimer tick: collect this controller's CPU/RSS via
    // ResourceStatsCollector::collect → m_monitorModel.mergeController; then,
    // if connected, send a get_stats command so the renderer emits a
    // stats_state response on its next tick (interface.md §F.1 / §K).
    // Guarded by m_connected — no point queueing a command the renderer
    // cannot receive (it would also leak a pending request that times out
    // at 10s per interface.md §7.2).
    void onMonitorTick();

    // stats_state EVENT handler (interface.md §K): parse the renderer payload
    // → RendererStats::fromJson → m_monitorModel.mergeRenderer. Replaces the
    // log-only default registered by EventRegistry::registerDefaults.
    // QMap::insert replaces, so the override always wins regardless of call
    // order. Missing/null fields are handled by fromJson's per-field guards.
    void handleStatsStateEvent(const Envelope& env);

    // Start (or restart) the idle Scheduler using m_modelInfo->motionGroups +
    // m_config.idleInterval. Called from the model_loaded handler. The callback
    // sends play_motion with priority=1 (Idle per interface.md §B.1).
    void startIdleScheduler();

private:
    // ── Owned per-instance tuple (M2: NOT shared from main.cpp) ──────────────
    // Declaration order = construction order. m_dispatcher MUST precede
    // m_eventRegistry (EventRegistry captures &m_dispatcher in its ctor).
    // m_server + m_pending MUST precede m_interactionHandler (its messageSender
    // lambda captures `this` and reads m_server — safe because the lambda body
    // is not invoked during construction, only when handleHitEvent runs later).
    InstanceConfig        m_config;
    MessageDispatcher     m_dispatcher;
    EventRegistry         m_eventRegistry;
    ProcessManager        m_processManager;
    StartupSalvo          m_salvo;
    WsServer&             m_server;
    PendingRequests&      m_pending;
    InstanceConfigManager m_configManager;     // todo 10: drag_end persistence
    InteractionHandler    m_interactionHandler; // todo 9/10: hit → play_motion
    HitAreaCacheManager   m_hitAreaCache;       // todo 10: hitArea cache by model
    Scheduler             m_scheduler;          // todo 8/10: idle motion cycling
    RestartController     m_restartController;  // todo 12: crash-recovery backoff
    // todo 20/21: voice-pack behavior engine. Empty = not mounted; mount
    // swaps in the parsed VoicePackInfo (owned by m_mountedPack).
    core::MountedBehaviorEngine m_behaviorEngine;
    std::optional<core::VoicePackInfo> m_mountedPack;

    // ── Dialogue sink seam (bubble-stream switch) ──────────────────────────
    // Injected by InstanceManager; empty = no-op. See the public setter.
    DialogueSinkFn m_dialogueSink;

    // Voice-pack behavior cooldown: while a behavior's motion/audio plays,
    // further hits on the model are swallowed (no re-trigger, no bubble).
    // Mimics the reference product: bubbles accompany PLAYING audio; clicks
    // during playback do not stack new ones.
    qint64 m_behaviorActiveUntilMs = 0;

    // ── Phase-5 Wave 8 todo 18 (resource monitor) ───────────────────────────
    // Per-session monitor model + the controller-side collector that feeds it
    // on a 2s QTimer. The poller is armed in the `ready` handler (status→
    // running) and stopped in stop() so a stopped instance does not keep
    // firing get_stats into the void. The model is exposed to QML via the
    // monitorModel() Q_INVOKABLE above; MonitorPage.qml owns the chart
    // rendering, this class owns ONLY the data feed.
    MonitorDataModel      m_monitorModel;
    ResourceStatsCollector m_statsCollector;
    QTimer                m_monitorTimer;

    // ── Runtime auth (derived per-session, NOT in InstanceConfig) ────────────
    // instanceId: a stable positive int derived from config.id (the WsServer
    // token map keys on int). token: 32-byte hex, regenerated each start().
    int     m_instanceId;
    QString m_token;

    // ── Renderer dir (stored in start() for model_loaded's model3.json path) ─
    QString m_rendererDir;

    // ── Live state ───────────────────────────────────────────────────────────
    QString m_status = QStringLiteral("stopped");
    bool    m_connected = false;
    bool    m_modelLoaded = false;
    std::optional<core::ModelInfo> m_modelInfo;

    // ── Model-load failure observability (模型库 B 档) ───────────────────────
    int     m_modelLoadFailureRevision = 0;
    QString m_lastModelLoadError;

    // ── Phase-5+ counters / flags (slots for later todos) ───────────────────
    int  m_idleMotionCount = 0;   // todo 8 (Scheduler) increments
    int  m_restartAttempts = 0;   // todo 12 (RestartController) reads
    bool m_manuallyStopping = false;
};

// Q_DECLARE_METATYPE for the pointer form so Q_INVOKABLE methods returning
// InstanceSession* (InstanceManager::instanceAt) cross the C++→QML boundary.
// Without this, QML prints "Unknown method return type: InstanceSession*"
// and the call yields undefined. QObject* return types that name a derived
// class need this registration; a bare QObject* return would work without it
// but loses static type info on the C++ side.
Q_DECLARE_METATYPE(InstanceSession*)
