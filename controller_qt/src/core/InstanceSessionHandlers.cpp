#include "core/InstanceSession.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

#include <spdlog/spdlog.h>
#include "api/EventIds.hpp"
#include "core/EventCatalog.hpp"
#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"

// InstanceSessionHandlers — Phase-5 todo 10 event-handler bodies extracted from
// InstanceSession.cpp to keep the orchestrator's main implementation under the
// 250 pure-LOC ceiling (same split discipline as InstanceSessionSetters.cpp +
// InstanceSessionCommands.cpp). Each method is invoked by a one-liner lambda
// registered in InstanceSession::start() via m_dispatcher.registerEventHandler.
//
// S8a (v1.5): the handlers below additionally PUBLISH their events onto the
// shared bus (publishHostEvent) — pure appends next to the untouched
// pre-existing logic lines. Zero behavior change: nothing reads
// event.cancelled, and a session without an injected publisher publishes
// nothing at all (identical to pre-S8a).
//
// S8b (v1.5): the hit DECISION chain moved onto the bus — handleHitEvent is
// now pure emission (cancellable), the two Lowest-tier adapters below
// (MountedBehaviorAdapter first, InteractionFallbackAdapter second) carry the
// pre-S8b glue verbatim and honor event.cancelled (mutual exclusion + plugin
// claims). idle_tick's callback reads publishHostEvent's return (final
// cancelled) and skips the play_motion when a listener claimed the tick.

void InstanceSession::setEventPublisher(pet::IEventApi* publisher)
{
    // S8b binding: (re)bind the hit-decision adapters along with the
    // publisher. MBE subscribes FIRST, IH SECOND — both at Lowest, so the
    // tier's FIFO order gives MBE priority exactly as the pre-S8b if/else
    // chain did. CONTRACT (audit-hardened): these two Lowest
    // subscriptions MUST remain ADJACENT with MBE FIRST — the MBE→IH
    // mutual exclusion depends on same-tier FIFO order (MBE claims the
    // hit, IH is the fallback), and any other subscription inserted
    // BETWEEN them silently breaks that dependency; read EventBus's
    // same-tier ordering semantics BEFORE inserting any other
    // subscription here. Re-injection first unsubscribes from the OLD
    // publisher (the bus holds raw pointers — a stale entry would
    // dangle).
    if (m_eventPublisher != nullptr) {
        m_eventPublisher->unsubscribe(pet::kEventInstanceHit.toString(),
                                      &m_mbeAdapter);
        m_eventPublisher->unsubscribe(pet::kEventInstanceHit.toString(),
                                      &m_ihAdapter);
    }
    m_eventPublisher = publisher;
    if (m_eventPublisher == nullptr)
        return; // tests / degraded wiring — no subscription, no emission
    m_eventPublisher->subscribe(pet::kEventInstanceHit.toString(),
                                &m_mbeAdapter, pet::EventPriority::Lowest);
    m_eventPublisher->subscribe(pet::kEventInstanceHit.toString(),
                                &m_ihAdapter, pet::EventPriority::Lowest);
}

bool InstanceSession::publishHostEvent(const QString& eventId, bool cancellable,
                                       const QJsonObject& payload)
{
    if (m_eventPublisher == nullptr)
        return false; // tests / degraded wiring — pre-S8a behavior
    pet::Event evt;
    evt.eventId = eventId;
    evt.instanceUuid = m_config.id;
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = cancellable;
    evt.payload = payload;
    // Synchronous dispatch (publish returns after the whole chain ran).
    m_eventPublisher->publish(evt);
    // S8b: hand the poster the FINAL cancelled value (idle_tick's trigger
    // callback consumes it; hit deliberately does not — each default-
    // behavior listener checks the flag itself).
    return evt.cancelled;
}

void InstanceSession::handleHitEvent(const Envelope& env)
{
    // S8b: PURE EMISSION — the decision chain (MountedBehaviorEngine →
    // InteractionHandler, pre-S8b a direct if/else here) now runs as the
    // two Lowest-tier bus adapters subscribed in setEventPublisher. This
    // function deliberately does NOT read event.cancelled: plugins at
    // Normal+ (and the rule engine at Low) see the hit first and may
    // claim it; the default adapters self-check the flag (Forge style).
    //
    // area_id is extracted from the payload verbatim (interface.md §F).
    // An absent area_id is treated as empty — the MBE adapter ignores it
    // and the IH adapter's 3-tier lookup no-ops, so a malformed hit is a
    // no-op (same as pre-S8b). Payload keys come from the EventCatalog
    // builder (events.md §hit verbatim); missing WS halves degrade to
    // their zero defaults.
    //
    // With NO publisher injected (degraded wiring / direct-construction
    // tests) nothing is published and no decision runs — hits are no-ops.
    const QString areaId = env.payload.value(
        QStringLiteral("area_id")).toString();
    publishHostEvent(pet::kEventInstanceHit, /*cancellable=*/true,
                     core::makeHitPayload(
                         areaId,
                         env.payload.value(QStringLiteral("x")).toDouble(),
                         env.payload.value(QStringLiteral("y")).toDouble(),
                         env.payload.value(QStringLiteral("button")).toInt()));
}

void InstanceSession::MountedBehaviorAdapter::onEvent(pet::Event& event)
{
    // Cross-instance filter: the shared bus fans pet.instance.hit to EVERY
    // session's adapters; react only to THIS session's uuid.
    if (event.instanceUuid != m_session->m_config.id)
        return;
    // A higher-priority listener (plugin at Normal+, the rule engine at
    // Low) already claimed the hit — the default chain stays quiet.
    if (event.cancelled)
        return;

    // todo 20 hit-decision glue, moved VERBATIM from the pre-S8b direct
    // call chain in handleHitEvent (architecture-blueprint.md §8.4).
    const QString areaId = event.payload.value(
        QStringLiteral("area_id")).toString();

    if (!m_session->m_behaviorEngine.hasGroupForArea(areaId))
        return; // not the pack's domain → leave uncancelled for the IH adapter

    // Cooldown gate: while the previous behavior's motion/audio is still
    // playing, swallow the hit entirely — no re-trigger, no bubble, and
    // (pre-S8b semantics) no InteractionHandler fallback either. Setting
    // cancelled keeps the later same-tier IH adapter quiet.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs < m_session->m_behaviorActiveUntilMs) {
        LOG_DEBUG("InstanceSession[{}]: hit during behavior cooldown "
                  "({}ms left) — ignored", m_session->m_instanceId,
                  int(m_session->m_behaviorActiveUntilMs - nowMs));
        event.cancelled = true;
        return;
    }
    auto result = m_session->m_behaviorEngine.buildBehaviorCommand(areaId);
    if (result.has_value()) {
        LOG_INFO("InstanceSession[{}]: hit area=\"{}\" → play_motion_ext "
                 "(voice pack)", m_session->m_instanceId,
                 areaId.toStdString());
        m_session->sendCommand(result->command);
        // Cooldown window = the behavior's audio length (estimated from
        // the OGG header; the engine's 5s fallback covers no-audio and
        // parse-failure cases).
        const QString audioPath = result->command.payload
            .value(QStringLiteral("audio_path")).toString();
        const qint64 audioMs = audioPath.isEmpty()
            ? 0
            : core::MountedBehaviorEngine::estimateOggDurationMs(audioPath);
        m_session->m_behaviorActiveUntilMs = nowMs + (audioMs > 0 ? audioMs : 4000);
        // Voice-pack dialogue text goes to the notification bubble stream
        // (never to the renderer — the subtitle side-channel is gone).
        // Bubble lifetime = audio length + 8s display margin; 0 = default.
        if (!result->dialogueText.isEmpty() && m_session->m_dialogueSink) {
            m_session->m_dialogueSink(m_session->m_config.id,
                                      m_session->label(),
                                      m_session->avatar(),
                                      result->dialogueText,
                                      audioMs > 0 ? int(audioMs + 8000) : 0);
        }
        m_session->m_scheduler.pause();
        // Mutual exclusion: MBE produced the behavior — cancel so the
        // later same-Lowest-tier IH adapter skips (subscription order
        // guarantees it runs after this one). This preserves the pre-S8b
        // "MBE handles → IH does not run" if/else semantics.
        event.cancelled = true;
        return;
    }
    // Engine reported a group but buildBehaviorCommand returned nullopt
    // (group has only audio-only actions, or no motion actions). Leave
    // the event uncancelled so the IH adapter still gives feedback.
    LOG_DEBUG("InstanceSession[{}]: voice-pack group for \"{}\" has no "
              "motion action → falling back to InteractionHandler",
              m_session->m_instanceId, areaId.toStdString());
}

void InstanceSession::InteractionFallbackAdapter::onEvent(pet::Event& event)
{
    // Cross-instance + claim filters (same discipline as the MBE adapter).
    if (event.instanceUuid != m_session->m_config.id)
        return;
    if (event.cancelled)
        return; // MBE handled it, the cooldown swallowed it, or a plugin claimed it

    const QString areaId = event.payload.value(
        QStringLiteral("area_id")).toString();
    LOG_DEBUG("InstanceSession[{}]: hit area=\"{}\" → InteractionHandler",
              m_session->m_instanceId, areaId.toStdString());
    // Rebuild the WS hit envelope shape from the bus payload (the catalog
    // payload mirrors the wire keys verbatim) and reuse InteractionHandler's
    // 3-tier case-fold lookup + play_motion build unchanged.
    Envelope env;
    env.type = QStringLiteral("event");
    env.action = QStringLiteral("hit");
    env.payload = event.payload;
    m_session->m_interactionHandler.handleHitEvent(env);
}

void InstanceSession::handleDragEndEvent(const Envelope& env)
{
    // interface.md §H: drag_end carries window_x + window_y as screen-pixel
    // ints. Extract them, update m_config, and persist via InstanceConfigManager
    // so the next launch restores the window position via set_position.
    //
    // QJsonValue::isDouble() is true for ALL JSON numbers (int and double);
    // toInteger() returns the qint64 value. Missing/null/non-numeric fields →
    // isDouble()==false → silent no-op (never partial-writes, never crashes).
    const QJsonValue xVal = env.payload.value(QStringLiteral("window_x"));
    const QJsonValue yVal = env.payload.value(QStringLiteral("window_y"));
    if (!xVal.isDouble() || !yVal.isDouble()) {
        LOG_DEBUG("InstanceSession[{}]: drag_end without window_x/window_y; ignoring",
                  m_instanceId);
        return;
    }

    m_config.windowX = static_cast<int>(xVal.toInteger());
    m_config.windowY = static_cast<int>(yVal.toInteger());

    if (m_configManager.save(m_config)) {
        LOG_INFO("InstanceSession[{}]: persisted window ({},{}) after drag_end",
                 m_instanceId, m_config.windowX, m_config.windowY);
    } else {
        LOG_ERROR("InstanceSession[{}]: failed to persist window position after drag_end",
                  m_instanceId);
    }

    // S8a (v1.5): post-commit publish — fires only AFTER the position is
    // persisted (or its failure logged); the bypass never gates the
    // persistence above. Malformed drag_end events (the early return)
    // publish nothing: no commit, no event.
    publishHostEvent(pet::kEventDragEnded, /*cancellable=*/false,
                     core::makeDragEndedPayload(
                         env.payload.value(QStringLiteral("x")).toDouble(),
                         env.payload.value(QStringLiteral("y")).toDouble(),
                         static_cast<int>(xVal.toInteger()),
                         static_cast<int>(yVal.toInteger())));
}

void InstanceSession::handleMotionFinishedEvent(const Envelope& env)
{
    // interface.md §E: motion_finished has TWO payload variants.
    //   Variant A (play_motion):     { group, index }
    //   Variant B (play_motion_ext): { motion_path }
    // Discriminator = presence of "motion_path".
    //
    // Idle-judgment policy (blueprint §8.3):
    //   - Variant A with group == "Idle" → idle → do NOT triggerNow (avoids a
    //     feedback loop: triggerNow → play Idle → motion_finished{Idle} →
    //     triggerNow → ...).
    //   - Variant A with group != "Idle" (e.g. "TapBody") → non-idle →
    //     triggerNow to resume the idle cadence sooner.
    //   - Variant B (motion_path) → non-idle for Phase 6 (idle-pack path
    //     detection arrives in Phase 8 todo 20) → triggerNow.
    // triggerNow only fires when connected — no point queuing a play_motion
    // the renderer cannot receive.
    const bool hasMotionPath = env.payload.contains(QStringLiteral("motion_path"));

    // S8a (v1.5): bypass publish at entry — motion_finished is pure
    // telemetry (this handler commits no local state before judging
    // idle/non-idle), and the builder normalizes BOTH wire variants into
    // the one bus shape (group:""/index:-1 vs motion_path:"" sentinels —
    // api/EventIds.hpp).
    if (hasMotionPath) {
        publishHostEvent(pet::kEventInstanceMotionFinished,
                         /*cancellable=*/false,
                         core::makeMotionFinishedPayload(
                             env.payload.value(QStringLiteral("motion_path"))
                                 .toString()));
    } else {
        publishHostEvent(pet::kEventInstanceMotionFinished,
                         /*cancellable=*/false,
                         core::makeMotionFinishedPayload(
                             env.payload.value(QStringLiteral("group"))
                                 .toString(),
                             env.payload.value(QStringLiteral("index"))
                                 .toInt()));
    }

    bool isIdle = false;
    if (hasMotionPath) {
        // Variant B — assume non-idle until Phase 8 adds idle-pack path matching.
        isIdle = false;
    } else {
        const QString group = env.payload.value(QStringLiteral("group")).toString();
        isIdle = group.compare(QStringLiteral("Idle"), Qt::CaseInsensitive) == 0;
    }

    if (isIdle) {
        LOG_DEBUG("InstanceSession[{}]: idle motion finished → no triggerNow",
                  m_instanceId);
        return;
    }

    if (!m_connected) {
        LOG_DEBUG("InstanceSession[{}]: non-idle motion finished but not connected; no triggerNow",
                  m_instanceId);
        return;
    }

    LOG_DEBUG("InstanceSession[{}]: non-idle motion finished → triggerNow", m_instanceId);
    m_scheduler.triggerNow();
}

void InstanceSession::startIdleScheduler()
{
    // Guard: need motionGroups from m_modelInfo. If the .model3.json failed to
    // parse, there is nothing to cycle — the scheduler stays stopped.
    if (!m_modelInfo.has_value() || m_modelInfo->motionGroups.isEmpty()) {
        LOG_WARN("InstanceSession[{}]: cannot start Scheduler — no motionGroups",
                 m_instanceId);
        return;
    }
    // idleInterval is in SECONDS (InstanceConfig); Scheduler takes milliseconds.
    const int intervalMs = m_config.idleInterval * 1000;

    // The callback sends play_motion with priority=1 (Idle priority per
    // interface.md §B.1). sendCommand serializes compact + pushes through
    // WsServer::sendText + emits commandSent — so idle-triggered play_motion
    // is observable in the unified command stream.
    m_scheduler.setIdleMotions(m_modelInfo->motionGroups);
    m_scheduler.start(
        intervalMs,
        m_modelInfo->motionGroups,
        [this](const QString& group, int index) {
            // S8b: publish + HONOR cancellation — a bus listener (a plugin
            // at Normal+, the rule engine at Low) may claim the tick; the
            // priority=1 play_motion is then skipped entirely. With no
            // publisher injected publishHostEvent returns false and the
            // idle cadence is unchanged (pre-S8a shape).
            const bool cancelled = publishHostEvent(
                pet::kEventInstanceIdleTick, /*cancellable=*/true,
                core::makeIdleTickPayload(group, index));
            if (cancelled) {
                LOG_DEBUG("InstanceSession[{}]: idle_tick (\"{}\", {}) "
                          "cancelled by a bus listener — play_motion skipped",
                          m_instanceId, group.toStdString(), index);
                return;
            }
            // priority=1 (Idle) per interface.md §B.1.
            // (todo 16: previously inline createCommand; now via typed factory.)
            sendCommand(Protocol::buildPlayMotion(group, index, 1));
        },
        Scheduler::kDefaultGroup);

    LOG_INFO("InstanceSession[{}]: Scheduler started (interval={}ms, idleGroup=\"{}\")",
             m_instanceId, intervalMs, Scheduler::kDefaultGroup.toStdString());
}

void InstanceSession::handleLayoutChangedEvent(const Envelope& env)
{
    // interface.md §I layout_changed payload: { offset_x, offset_y, scale }.
    // Emitted on Shift+drag / Shift+scroll user layout edits. Persist so the
    // next launch restores the layout via set_layout in the startup salvo.
    // Missing/null/non-numeric → no-op (never partial-writes, never crashes).
    const QJsonValue ox = env.payload.value(QStringLiteral("offset_x"));
    const QJsonValue oy = env.payload.value(QStringLiteral("offset_y"));
    const QJsonValue sc = env.payload.value(QStringLiteral("scale"));
    if (!ox.isDouble() || !oy.isDouble() || !sc.isDouble()) {
        LOG_DEBUG("InstanceSession[{}]: layout_changed without offset_x/offset_y/scale; ignoring",
                  m_instanceId);
        return;
    }

    m_config.layoutOffsetX = ox.toDouble();
    m_config.layoutOffsetY = oy.toDouble();
    m_config.layoutScale   = sc.toDouble();

    if (m_configManager.save(m_config)) {
        LOG_INFO("InstanceSession[{}]: persisted layout (ox={}, oy={}, scale={}) after layout_changed",
                 m_instanceId,
                 m_config.layoutOffsetX, m_config.layoutOffsetY, m_config.layoutScale);
    } else {
        LOG_ERROR("InstanceSession[{}]: failed to persist layout after layout_changed",
                  m_instanceId);
    }
}

void InstanceSession::handleWindowResizedEvent(const Envelope& env)
{
    // interface.md §J window_resized payload: { window_width, window_height,
    // window_x, window_y }. Emitted on Ctrl+scroll (center-preserving resize).
    // Persist so the next launch restores via set_size + set_position.
    // Missing/null/non-numeric → no-op (never partial-writes, never crashes).
    const QJsonValue ww = env.payload.value(QStringLiteral("window_width"));
    const QJsonValue wh = env.payload.value(QStringLiteral("window_height"));
    const QJsonValue wx = env.payload.value(QStringLiteral("window_x"));
    const QJsonValue wy = env.payload.value(QStringLiteral("window_y"));
    if (!ww.isDouble() || !wh.isDouble() ||
        !wx.isDouble() || !wy.isDouble()) {
        LOG_DEBUG("InstanceSession[{}]: window_resized without window_width/height/x/y; ignoring",
                  m_instanceId);
        return;
    }

    m_config.windowWidth  = static_cast<int>(ww.toInteger());
    m_config.windowHeight = static_cast<int>(wh.toInteger());
    m_config.windowX      = static_cast<int>(wx.toInteger());
    m_config.windowY      = static_cast<int>(wy.toInteger());

    if (m_configManager.save(m_config)) {
        LOG_INFO("InstanceSession[{}]: persisted window ({}x{} @ {},{}) after window_resized",
                 m_instanceId,
                 m_config.windowWidth, m_config.windowHeight,
                 m_config.windowX, m_config.windowY);
    } else {
        LOG_ERROR("InstanceSession[{}]: failed to persist window after window_resized",
                  m_instanceId);
    }
}

void InstanceSession::handleLayoutStateEvent(const Envelope& env)
{
    // interface.md §L layout_state: the pseudo-response to get_layout (§E.2),
    // routed by ACTION not by id (§7.3). Sync m_config + emit layoutUpdated so
    // the UI refreshes. NO persist — this is a read-back of current renderer
    // state; the values are already on disk or will be when the user edits via
    // layout_changed. Missing/non-numeric → no-op.
    const QJsonValue ox = env.payload.value(QStringLiteral("offset_x"));
    const QJsonValue oy = env.payload.value(QStringLiteral("offset_y"));
    const QJsonValue sc = env.payload.value(QStringLiteral("scale"));
    if (!ox.isDouble() || !oy.isDouble() || !sc.isDouble()) {
        LOG_DEBUG("InstanceSession[{}]: layout_state without offset_x/offset_y/scale; ignoring",
                  m_instanceId);
        return;
    }

    m_config.layoutOffsetX = ox.toDouble();
    m_config.layoutOffsetY = oy.toDouble();
    m_config.layoutScale   = sc.toDouble();
    emit layoutUpdated(m_config.layoutOffsetX, m_config.layoutOffsetY,
                       m_config.layoutScale);
    LOG_DEBUG("InstanceSession[{}]: layout_state → synced (ox={}, oy={}, scale={})",
              m_instanceId,
              m_config.layoutOffsetX, m_config.layoutOffsetY, m_config.layoutScale);
}

void InstanceSession::handleModelLoadFailedEvent(const Envelope& env)
{
    // interface.md §B model_load_failed payload: { error_code, error_message }.
    // The renderer rejected a load_model (missing model dir, corrupt
    // .model3.json, ...). 模型库 B 档: surface the failure to QML — bump the
    // revision counter (drives Q_INVOKABLE binding re-evaluation) + record
    // the message + emit modelLoadFailed. modelLoaded is cleared: the
    // renderer tears the previous model down before attempting the load, so
    // the UI must not keep showing the old model as live. A missing/empty
    // error_message degrades to a generic string (never empty — the UI binds
    // non-empty text). Never throws, never touches the disk.
    const QString message = env.payload.value(
        QStringLiteral("error_message")).toString();
    m_lastModelLoadError = message.isEmpty()
        ? QStringLiteral("model load failed (no error_message)")
        : message;
    ++m_modelLoadFailureRevision;
    setModelLoaded(false);
    LOG_WARN("InstanceSession[{}]: model_load_failed rev={} error=\"{}\"",
             m_instanceId, m_modelLoadFailureRevision,
             m_lastModelLoadError.toStdString());
    emit modelLoadFailed(m_lastModelLoadError);
    // S8a (v1.5): post-commit publish — after the revision bump, the
    // modelLoaded flip and the modelLoadFailed signal. Payload halves are
    // the VERBATIM wire values (the degraded UI string above stays a QML
    // concern; "" is the bus-side sentinel for "wire event carried none").
    publishHostEvent(pet::kEventModelLoadFailed, /*cancellable=*/false,
                     core::makeModelLoadFailedPayload(
                         env.payload.value(QStringLiteral("error_code")).toInt(),
                         message));
}

void InstanceSession::handleAudioFinishedEvent(const Envelope& env)
{
    // events.md §15: audio_finished fires when an audio track reaches its
    // NATURAL end (data source exhausted); stop_audio and preemption
    // deliberately never emit it. Pure telemetry on the panel side: this
    // override REPLACES the EventRegistry default (registered in start(),
    // after registerDefaults), so it reproduces the default's DEBUG log
    // line verbatim (zero log change) and additionally fans the event
    // onto the bus with the VERBATIM WS payload (audio_path is already
    // snake_case — the makeAudioFinishedPayload passthrough). No other
    // behavior: the bubble stream runs its own display timers, and the
    // behavior cooldown is wall-clock based, so nothing keys off this.
    LOG_DEBUG("event action='audio_finished' payload={}",
              QString::fromUtf8(QJsonDocument(env.payload).toJson(
                                    QJsonDocument::Compact))
                  .toStdString());
    publishHostEvent(pet::kEventInstanceAudioFinished,
                     /*cancellable=*/false,
                     core::makeAudioFinishedPayload(env.payload));
}
