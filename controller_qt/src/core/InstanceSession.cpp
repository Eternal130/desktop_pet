#include "core/InstanceSession.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtGlobal>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/ModelInfoParser.hpp"
#include "core/PathResolve.hpp"
#include "network/PendingRequests.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

// Status string constants used across the session lifecycle. Centralized so a
// typo in one call site can never invent a new status the QML switch can't
// match. The values mirror the Java controller's InstanceSession states.
namespace {
constexpr const char* kStatusStopped    = "stopped";
constexpr const char* kStatusPending    = "pending";
constexpr const char* kStatusConnecting = "connecting";
constexpr const char* kStatusRunning    = "running";
constexpr const char* kStatusError      = "error";

// M1 fix: map the 28-field InstanceConfig to the 16-field InstanceConfigLike
// view that StartupSalvo::sendSalvo consumes. InstanceConfigLike is NOT
// implicitly convertible from InstanceConfig (different struct types), so this
// explicit field-by-field copy is required. The 16 fields are exactly the
// overlap: the salvo's 9 commands read these and nothing else from the config.
InstanceConfigLike toInstanceConfigLike(const InstanceConfig& cfg)
{
    InstanceConfigLike v;
    v.modelName         = cfg.modelName;
    v.windowX           = cfg.windowX;
    v.windowY           = cfg.windowY;
    v.windowWidth       = cfg.windowWidth;
    v.windowHeight      = cfg.windowHeight;
    v.opacity           = cfg.opacity;
    v.targetFps         = cfg.targetFps;
    v.volume            = cfg.volume;
    v.muted             = cfg.muted;
    v.layoutOffsetX     = cfg.layoutOffsetX;
    v.layoutOffsetY     = cfg.layoutOffsetY;
    v.layoutScale       = cfg.layoutScale;
    v.subtitleOffsetX   = cfg.subtitleOffsetX;
    v.subtitleOffsetY   = cfg.subtitleOffsetY;
    v.subtitleAreaWidth = cfg.subtitleAreaWidth;
    v.subtitleAreaHeight= cfg.subtitleAreaHeight;
    v.subtitleStylePreset = cfg.subtitleStylePreset;
    return v;
}

} // namespace

InstanceSession::InstanceSession(InstanceConfig config, WsServer& server,
                                 PendingRequests& pending,
                                 const QString& configBasePath,
                                 QObject* parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_dispatcher(this)
    , m_eventRegistry(&m_dispatcher, this)
    , m_processManager(this)
    , m_salvo(this)
    , m_server(server)
    , m_pending(pending)
    , m_configManager(configBasePath)
    // The messageSender captures `this` — safe because it is NOT invoked during
    // construction (only when handleHitEvent runs, post-start). InteractionHandler
    // ALWAYS sends play_motion, so commandSent re-emits that fixed action. This
    // keeps the unified commandSent stream observable from tests/QML.
    // Phase 5 todo 11: sendText routes by instanceId; m_instanceId is THIS
    // session's routing key (the WsServer holds one socket per instance_id).
    , m_interactionHandler([this](const QString& json) {
          m_server.sendText(m_instanceId, json);
          emit commandSent(QStringLiteral("play_motion"));
      }, this)
    , m_hitAreaCache(configBasePath, this)
    , m_scheduler(this)
    , m_restartController(this)
    , m_monitorModel(this)
    , m_statsCollector()
    , m_monitorTimer(this)
{
    // Derive a stable positive int from the config UUID. The WsServer token
    // registry (QMap<int, QString>) keys on an int instance_id; InstanceConfig
    // holds only a string UUID, so a deterministic hash gives a stable key that
    // survives restarts of the same persisted instance. qHash of a non-empty
    // QString is well-defined in Qt6.
    m_instanceId = static_cast<int>(qHash(m_config.id) & 0x7FFFFFFF);

    // Wire the response handler: every type=="response" envelope flows through
    // the shared PendingRequests table (M2 — instance-agnostic id matching).
    m_dispatcher.setResponseHandler(
        [this](const Envelope& env) { m_pending.handleResponse(env); });

    // Wire the salvo's command sender → WsServer::sendText (production wiring).
    // Same pattern as ProcessManager::setShutdownSender below.
    // Phase 5 todo 11: sendText routes by m_instanceId (per-instance socket).
    m_salvo.setCommandSender([this](const QString& json) {
        m_server.sendText(m_instanceId, json);
    });
    // Re-emit the salvo's per-command signal through InstanceSession so tests
    // (QSignalSpy) and QML can observe the full outbound command stream from a
    // single signal, without reaching into the private m_salvo member.
    connect(&m_salvo, &StartupSalvo::commandSent, this,
            [this](const QString& action, const QString&) {
                emit commandSent(action);
            });

    // Re-emit Scheduler::triggered as InstanceSession::idleMotionTriggered so
    // tests (QSignalSpy) and QML can observe scheduler activity without reaching
    // into the private m_scheduler member.
    connect(&m_scheduler, &Scheduler::triggered, this,
            [this](const QString& group, int index) {
                emit idleMotionTriggered(group, index);
            });

    // RestartController wiring (todo 12). The retry signal fires after a
    // QTimer::singleShot backoff delay; the renderer process has already
    // exited by then (QProcess::NotRunning), so calling start() re-launches
    // it cleanly. m_manuallyStopping is guaranteed false here — the only
    // path to scheduleRestart is onProcessExited's `!m_manuallyStopping &&
    // crashed` branch (early-return otherwise). The gaveUp signal flips the
    // session to terminal error after MAX_ATTEMPTS retries (5 by default).
    connect(&m_restartController, &RestartController::retry, this, [this]() {
        LOG_INFO("InstanceSession[{}]: restartController.retry → start",
                 m_instanceId);
        start();
    });
    connect(&m_restartController, &RestartController::gaveUp, this, [this]() {
        LOG_ERROR("InstanceSession[{}]: restartController.gaveUp — max restart "
                  "attempts reached", m_instanceId);
        setStatus(kStatusError);
        emit startFailed(QStringLiteral("max restart attempts reached"));
    });

    // ProcessManager::exited → crash-vs-user-stop routing.
    connect(&m_processManager, &ProcessManager::exited,
            this, &InstanceSession::onProcessExited);

    // WsServer connection-state → track m_connected for this instance. Cast
    // the enum to int for the slot signature (avoids pulling the enum into the
    // header's onConnectionStateChanged signature; todo 11 routes per-instance).
    connect(&m_server, &WsServer::connectionStateChanged,
            this, [this](WsConnectionState state, const ConnectionInfo& info) {
                onConnectionStateChanged(static_cast<int>(state), info.instanceId);
            });

    // Wave 8 todo 18: 2s monitor poller. m_monitorTimer is configured here but
    // NOT started — start() arms it on `ready` (status=running), stop() stops
    // it. The timer parent is `this` so InstanceSession destruction cancels
    // any in-flight tick safely (no use-after-free through the lambda capture).
    m_monitorTimer.setInterval(2000);
    m_monitorTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_monitorTimer, &QTimer::timeout, this, [this]() {
        onMonitorTick();
    });

    // todo 21: restore a persisted voice-pack mount so hit events route
    // through MountedBehaviorEngine immediately after launch.
    if (!m_config.voicePack.isEmpty()) {
        auto pack = core::parseMetaMko(m_config.voicePack);
        if (pack.has_value()) {
            m_mountedPack = std::move(*pack);
            m_behaviorEngine.setVoicePack(&*m_mountedPack);
            LOG_INFO("InstanceSession[{}]: restored voice pack \"{}\"",
                     m_instanceId, m_mountedPack->displayName.toStdString());
        } else {
            LOG_WARN("InstanceSession[{}]: persisted voice pack \"{}\" no "
                     "longer parses — starting unmounted", m_instanceId,
                     m_config.voicePack.toStdString());
        }
    }
}

InstanceSession::~InstanceSession() = default;

// ── Read accessors ───────────────────────────────────────────────────────────

QString InstanceSession::label() const      { return m_config.label; }
QString InstanceSession::avatar() const {
    return m_config.avatar.isEmpty() ? QStringLiteral("🐱") : m_config.avatar;
}
QString InstanceSession::uuid() const       { return m_config.id; }
QString InstanceSession::modelName() const   { return m_config.modelName; }
QString InstanceSession::status() const      { return m_status; }
bool    InstanceSession::connected() const   { return m_connected; }
bool    InstanceSession::modelLoaded() const { return m_modelLoaded; }
double  InstanceSession::opacity() const     { return m_config.opacity; }
int     InstanceSession::targetFps() const   { return m_config.targetFps; }
double  InstanceSession::volume() const      { return m_config.volume; }
bool    InstanceSession::muted() const       { return m_config.muted; }
int     InstanceSession::instanceId() const  { return m_instanceId; }

const InstanceConfig& InstanceSession::config() const { return m_config; }

std::optional<core::ModelInfo> InstanceSession::modelInfo() const { return m_modelInfo; }

// ── Lifecycle ────────────────────────────────────────────────────────────────

void InstanceSession::start()
{
    LOG_INFO("InstanceSession::start instanceId={} id=\"{}\" model=\"{}\" backend=\"{}\"",
             m_instanceId, m_config.id.toStdString(),
             m_config.modelName.toStdString(), m_config.graphicsBackend.toStdString());

    setStatus(kStatusPending);

    // The shared WsServer must already be listening — the renderer connects
    // back to its port. In production main.cpp calls listen(9001) before any
    // session starts; in tests the WsServer listens on port 0 (auto-assigned).
    const quint16 port = m_server.serverPort();
    if (port == 0) {
        const QString reason = QStringLiteral("WsServer is not listening");
        LOG_ERROR("InstanceSession::start failed: {}", reason.toStdString());
        setStatus(kStatusError);
        emit startFailed(reason);
        return;
    }

    // Resolve the renderer executable. config.rendererPath (when non-empty) is
    // the renderer DIRECTORY — lets tests inject the real build/bin path. When
    // empty, fall back to the conventional defaultRendererDir() (app-dir +
    // "/../build/bin", where build.py places the exes side-by-side).
    const QString rendererDir = m_config.rendererPath.isEmpty()
        ? core::defaultRendererDir()
        : m_config.rendererPath;
    m_rendererDir = rendererDir; // stored for model_loaded's model3.json path
    const auto path = core::resolveRendererPath(rendererDir, m_config.graphicsBackend);
    if (!path.has_value()) {
        const QString reason = QStringLiteral(
            "renderer executable not found (backend=\"%1\", dir=\"%2\")")
            .arg(m_config.graphicsBackend, rendererDir);
        LOG_ERROR("InstanceSession::start failed: {}", reason.toStdString());
        setStatus(kStatusError);
        emit startFailed(reason);
        return;
    }

    // Generate a fresh per-start token and register it on the shared WsServer
    // BEFORE launching the renderer (avoids the connect-before-register race).
    m_token = generateToken();
    m_server.registerToken(m_instanceId, m_token);

    // Wire the shutdown sender — ProcessManager::stop() stage-1 calls this to
    // push the shutdown {} command over WS. Mirrors main.cpp's todo-1 wiring.
    m_processManager.setShutdownSender([this]() {
        const QByteArray json =
            serialize(Protocol::buildShutdown()).toJson(QJsonDocument::Compact);
        m_server.sendText(m_instanceId, QString::fromUtf8(json));
        emit commandSent(QStringLiteral("shutdown"));
    });

    // Register the default log-only event handlers for all 13 protocol events,
    // THEN override `ready` + `model_loaded` (QMap::insert replaces — the later
    // call wins). The overrides implement the blueprint §8.1 lifecycle.
    m_eventRegistry.registerDefaults();

    // ready → status=running → fire the 9-command startup salvo (blueprint
    // §8.1 step 6-7). The salvo is the controller's first substantive act
    // after the renderer signals readiness. sendSalvo pushes load_model,
    // set_position, set_size, set_opacity, set_fps, set_volume, set_layout,
    // set_subtitle_layout, set_subtitle_style — in that exact order.
    m_dispatcher.registerEventHandler(
        QStringLiteral("ready"), [this](const Envelope&) {
            LOG_INFO("InstanceSession[{}]: ready → running + salvo", m_instanceId);
            setStatus(kStatusRunning);
            setConnected(true);
            m_salvo.sendSalvo(toInstanceConfigLike(m_config));
            // Wave 8 todo 18: arm the 2s monitor poller now that the renderer
            // is up. Each tick: collect controller stats + send get_stats; the
            // response arrives via the stats_state EVENT handler registered
            // below. Stopped in stop() to avoid spamming a dead socket.
            m_monitorTimer.start();
        });

    // model_loaded → parse the .model3.json for hit areas (the renderer's
    // model_loaded event carries EMPTY arrays — interface.md §B) → consult the
    // HitAreaCacheManager for cached hitAreas → send set_hit_areas → store
    // ModelInfo → start the idle Scheduler → setModelLoaded(true). A missing/
    // corrupt .model3.json logs WARN but does NOT fail: set_hit_areas with an
    // empty array is legal, and the UI shows "no motions" gracefully.
    m_dispatcher.registerEventHandler(
        QStringLiteral("model_loaded"), [this](const Envelope&) {
            LOG_INFO("InstanceSession[{}]: model_loaded → parse + set_hit_areas",
                     m_instanceId);
            const QString model3Path = resolveModel3JsonPath();
            const auto info = core::parseModelInfo(model3Path);
            if (info.has_value()) {
                m_modelInfo = info;

                // Prefer cached hitAreas (todo 10); fall back to parsed + cache
                // the result so the next switch to this model reuses them.
                QStringList hitAreas;
                const auto cached = m_hitAreaCache.get(m_config.modelName);
                if (cached.has_value()) {
                    hitAreas = *cached;
                } else {
                    hitAreas = info->hitAreas;
                    m_hitAreaCache.put(m_config.modelName, hitAreas);
                }

                sendCommand(Protocol::buildSetHitAreas(
                    QJsonArray::fromStringList(hitAreas)));
                LOG_INFO("InstanceSession[{}]: sent set_hit_areas (count={})",
                         m_instanceId, hitAreas.size());
            } else {
                LOG_WARN("InstanceSession[{}]: could not parse \"{}\"; "
                         "proceeding with empty hitAreas",
                         m_instanceId, model3Path.toStdString());
            }
            // Re-apply persisted user layout (Wave 8 todo 22): load_model
            // recreates the LAppModel with default offset/scale, so the
            // controller must re-push set_layout so the user's saved layout
            // survives a model load/switch. Sent unconditionally — the renderer
            // confirmed model_loaded, so the model exists regardless of whether
            // the .model3.json parsed on the controller side.
            sendCommand(Protocol::buildSetLayout(
                m_config.layoutOffsetX, m_config.layoutOffsetY,
                m_config.layoutScale));
            setModelLoaded(true);
            // Start idle-motion cycling now that motionGroups are known.
            startIdleScheduler();
        });

    // Phase-5 todo 10 event-handler overrides. QMap::insert replaces the
    // log-only default from registerDefaults(); each lambda delegates to a
    // private method in InstanceSessionHandlers.cpp.
    m_dispatcher.registerEventHandler(
        QStringLiteral("hit"), [this](const Envelope& env) {
            handleHitEvent(env);
        });
    m_dispatcher.registerEventHandler(
        QStringLiteral("drag_end"), [this](const Envelope& env) {
            handleDragEndEvent(env);
        });
    m_dispatcher.registerEventHandler(
        QStringLiteral("motion_finished"), [this](const Envelope& env) {
            handleMotionFinishedEvent(env);
        });

    // Phase-5 Wave 8 todo 21 + M6: subtitle_layout_changed is the renderer's
    // 14th event (Protocol.hpp:39; emitted 3× in LAppDelegate.cpp). NOT in
    // EventRegistry's default 13 (kActiveEvents + kPhase6Events) — must be
    // registered explicitly here, else the dispatcher logs
    // "no handler for action" and the layout drift goes unpersisted.
    m_dispatcher.registerEventHandler(
        QStringLiteral("subtitle_layout_changed"), [this](const Envelope& env) {
            handleSubtitleLayoutChangedEvent(env);
        });

    // Phase-5 Wave 8 todo 22: layout_changed / window_resized / layout_state
    // overrides. layout_changed + window_resized are in EventRegistry's default
    // 13 (kActiveEvents) — this override replaces the log-only default with the
    // persist path. layout_state is the pseudo-response to get_layout (§7.3),
    // also in the default 13 — this override syncs m_config + emits
    // layoutUpdated. QMap::insert replaces, so the override wins.
    m_dispatcher.registerEventHandler(
        QStringLiteral("layout_changed"), [this](const Envelope& env) {
            handleLayoutChangedEvent(env);
        });
    m_dispatcher.registerEventHandler(
        QStringLiteral("window_resized"), [this](const Envelope& env) {
            handleWindowResizedEvent(env);
        });
    m_dispatcher.registerEventHandler(
        QStringLiteral("layout_state"), [this](const Envelope& env) {
            handleLayoutStateEvent(env);
        });

    // Wave 8 todo 18: stats_state override (interface.md §K — pseudo-response
    // to get_stats, routed by ACTION). Replaces the log-only default from
    // registerDefaults() so the renderer payload reaches m_monitorModel.
    m_dispatcher.registerEventHandler(
        QStringLiteral("stats_state"), [this](const Envelope& env) {
            handleStatsStateEvent(env);
        });

    // Launch the renderer. The renderer connects back to 127.0.0.1:kWsPort with
    // ?instance_id=<id>&token=<token>; WsServer's 3-gate handshake validates it.
    m_processManager.startRenderer(
        *path, port, m_instanceId, m_token, m_config.modelName,
        m_config.windowX, m_config.windowY,
        m_config.windowWidth, m_config.windowHeight);

    setStatus(kStatusConnecting);
}

void InstanceSession::stop()
{
    LOG_INFO("InstanceSession::stop instanceId={} manuallyStopping={}",
             m_instanceId, m_manuallyStopping);

    // Stop the idle Scheduler BEFORE the process teardown — no point firing
    // play_motion at a renderer that is about to exit. shutdown() is idempotent.
    m_scheduler.shutdown();

    // Wave 8 todo 18: stop the 2s monitor poller so a stopped instance does
    // not keep firing get_stats at a dead socket. Also clear the model so the
    // next start() (if any) begins with a fresh trend buffer — the prior
    // instance's data is stale after a renderer restart.
    m_monitorTimer.stop();
    m_monitorModel.clearHistory();

    // Set the flag BEFORE stop() so onProcessExited (which fires during the
    // blocking poll inside ProcessManager::stop) observes manuallyStopping=true
    // and does NOT emit startFailed for the known teardown exit.
    m_manuallyStopping = true;

    // ProcessManager::stop is the 3-stage graceful shutdown (send shutdown →
    // poll up to 5s → force-kill). Blocking — call from the main thread.
    m_processManager.stop();

    setConnected(false);
    setModelLoaded(false);
    setStatus(kStatusStopped);
}

void InstanceSession::restart()
{
    LOG_INFO("InstanceSession::restart instanceId={} attempts={}",
             m_instanceId, m_restartAttempts + 1);
    ++m_restartAttempts;
    stop();
    // Reset the flag so the new lifecycle's exit (if any) is treated as a crash
    // and not a residual user-stop. TODO(todo-12): RestartController policy.
    m_manuallyStopping = false;
    start();
}

void InstanceSession::loadModel(const QString& modelName)
{
    LOG_INFO("InstanceSession::loadModel instanceId={} model=\"{}\"",
             m_instanceId, modelName.toStdString());
    m_config.modelName = modelName;
    emit modelNameChanged();
    setModelLoaded(false);
    sendCommand(Protocol::buildLoadModel(modelName));
}

void InstanceSession::onMessage(const Envelope& env)
{
    m_dispatcher.dispatch(env);
}

// ── Private helpers ──────────────────────────────────────────────────────────
// sendCommand + the 4 Q_INVOKABLE setters live in InstanceSessionSetters.cpp
// (extracted to keep this file under the 250 pure-LOC ceiling).

void InstanceSession::setStatus(const QString& status)
{
    if (m_status == status)
        return;
    m_status = status;
    LOG_DEBUG("InstanceSession[{}]: status → \"{}\"", m_instanceId,
              status.toStdString());
    emit statusChanged();
}

void InstanceSession::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged();
}

void InstanceSession::setModelLoaded(bool loaded)
{
    if (m_modelLoaded == loaded)
        return;
    m_modelLoaded = loaded;
    emit modelLoadedChanged();
}

void InstanceSession::onProcessExited(int exitCode, bool crashed)
{
    LOG_INFO("InstanceSession[{}]::onProcessExited exitCode={} crashed={} manuallyStopping={}",
             m_instanceId, exitCode, crashed, m_manuallyStopping);

    // User-initiated stop (stop() set the flag) — the exit is expected, never a
    // crash. status is already "stopped" (set by stop() after the blocking poll).
    if (m_manuallyStopping)
        return;

    // Unexpected exit — clear live state. crashed → error + startFailed so QML
    // shows the reason; a clean exit (code 0, no crash) is still unexpected for
    // a running pet, so we report it as an error too. The RestartController
    // (todo 12) decides whether to retry: only a CRASH arms the backoff ladder
    // (a clean exit is treated as a deliberate shutdown — no restart attempt).
    setConnected(false);
    setModelLoaded(false);
    setStatus(kStatusError);
    const QString reason = crashed
        ? QStringLiteral("renderer crashed (exitCode=%1)").arg(exitCode)
        : QStringLiteral("renderer exited unexpectedly (code=%1)").arg(exitCode);
    emit startFailed(reason);

    // todo 12: only an unexpected CRASH triggers the exponential-backoff
    // retry ladder. scheduleRestart arms QTimer::singleShot(BACKOFF_MS[..])
    // → emits retry() → re-calls start(). After MAX_ATTEMPTS (5) it emits
    // gaveUp() instead, which the ctor-wired slot flips back to error +
    // startFailed("max restart attempts reached"). A clean exit (crashed==
    // false) does NOT arm a retry — the renderer shut itself down on purpose.
    if (crashed) {
        LOG_INFO("InstanceSession[{}]: crashed → RestartController.scheduleRestart",
                 m_instanceId);
        m_restartController.scheduleRestart();
    }
}

void InstanceSession::onConnectionStateChanged(int state, int instanceId)
{
    // Phase 5 todo 11: multi-instance routing. WsServer emits this for every
    // connection state change across ALL instances; ignore any that don't
    // match THIS session's instance_id. Without this filter, every session
    // would flip its m_connected flag whenever any other instance's renderer
    // connected or dropped.
    if (instanceId != m_instanceId)
        return;
    const bool nowConnected = (state == static_cast<int>(WsConnectionState::Connected));
    LOG_DEBUG("InstanceSession[{}]: connectionState → {} (connected={})",
             m_instanceId, state, nowConnected);
    setConnected(nowConnected);

    // todo 12: a successful WS reconnect means the renderer recovered — reset
    // the RestartController backoff ladder so the next crash starts fresh from
    // BACKOFF_MS[0] (2s) instead of resuming at the prior attempt's rung.
    // Only reset on the UP transition (Connected); a Disconnect does NOT
    // advance the counter (the next crash after a flappy disconnect still
    // counts against the same attempt sequence).
    if (nowConnected) {
        m_restartController.resetOnConnect();
    }
}

QString InstanceSession::generateToken()
{
    // 32 random bytes → 64-char hex string. Mirrors the Java
    // ProcessManager.generateSecureToken (SecureRandom + HexFormat). Uses the
    // system CSPRNG via QRandomGenerator::system().
    auto* gen = QRandomGenerator::system();
    QByteArray bytes(32, '\0');
    for (int i = 0; i < 32; ++i)
        bytes[i] = static_cast<char>(gen->bounded(0, 256));
    return QString::fromLatin1(bytes.toHex());
}

QString InstanceSession::resolveModel3JsonPath() const
{
    // The renderer ships Resources/Models/<modelName>/<modelName>.model3.json
    // next to its exe (architecture-blueprint.md §3.4). m_rendererDir is the
    // resolved renderer directory stored in start(); the model3.json lives
    // under its Resources/ subtree.
    return QDir(m_rendererDir).absoluteFilePath(
        QStringLiteral("Resources/Models/%1/%1.model3.json")
            .arg(m_config.modelName));
}
