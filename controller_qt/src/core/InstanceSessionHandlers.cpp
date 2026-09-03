#include "core/InstanceSession.hpp"

#include <QDateTime>
#include <QJsonObject>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"

// InstanceSessionHandlers — Phase-5 todo 10 event-handler bodies extracted from
// InstanceSession.cpp to keep the orchestrator's main implementation under the
// 250 pure-LOC ceiling (same split discipline as InstanceSessionSetters.cpp +
// InstanceSessionCommands.cpp). Each method is invoked by a one-liner lambda
// registered in InstanceSession::start() via m_dispatcher.registerEventHandler.

void InstanceSession::handleHitEvent(const Envelope& env)
{
    // todo 20 hit-decision priority chain (architecture-blueprint.md §8.4):
    //   1. If MountedBehaviorEngine has a group for the hit area_id → build a
    //      play_motion_ext command (motion + audio + lipSync + subtitle side-
    //      channels from the voice pack), pause the idle Scheduler (the voice-
    //      pack motion plays in lieu of idle), and return. The motion_finished
    //      handler will resume the cadence via triggerNow.
    //   2. Else → delegate to InteractionHandler (3-tier case-fold lookup →
    //      play_motion). The Phase-6 default path for models without a voice
    //      pack mounted.
    //
    // area_id is extracted from the payload verbatim (interface.md §F). An
    // absent area_id is treated as empty → hasGroupForArea returns false → the
    // InteractionHandler fallback also no-ops, so a malformed hit is a no-op.
    const QString areaId = env.payload.value(
        QStringLiteral("area_id")).toString();

    if (m_behaviorEngine.hasGroupForArea(areaId)) {
        // Cooldown gate: while the previous behavior's motion/audio is still
        // playing, swallow the hit entirely — no re-trigger, no bubble.
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs < m_behaviorActiveUntilMs) {
            LOG_DEBUG("InstanceSession[{}]: hit during behavior cooldown "
                      "({}ms left) — ignored", m_instanceId,
                      int(m_behaviorActiveUntilMs - nowMs));
            return;
        }
        auto result = m_behaviorEngine.buildBehaviorCommand(areaId);
        if (result.has_value()) {
            LOG_INFO("InstanceSession[{}]: hit area=\"{}\" → play_motion_ext "
                     "(voice pack)", m_instanceId, areaId.toStdString());
            sendCommand(result->command);
            // Cooldown window = the behavior's audio length (estimated from
            // the OGG header; the engine's 5s fallback covers no-audio and
            // parse-failure cases).
            const QString audioPath = result->command.payload
                .value(QStringLiteral("audio_path")).toString();
            const qint64 audioMs = audioPath.isEmpty()
                ? 0
                : core::MountedBehaviorEngine::estimateOggDurationMs(audioPath);
            m_behaviorActiveUntilMs = nowMs + (audioMs > 0 ? audioMs : 4000);
            // Voice-pack dialogue text goes to the notification bubble stream
            // (never to the renderer — the subtitle side-channel is gone).
            // Bubble lifetime = audio length + 8s display margin; 0 = default.
            if (!result->dialogueText.isEmpty() && m_dialogueSink) {
                m_dialogueSink(m_config.id, label(), avatar(),
                               result->dialogueText,
                               audioMs > 0 ? int(audioMs + 8000) : 0);
            }
            m_scheduler.pause();
            return;
        }
        // Engine reported a group but buildBehaviorCommand returned nullopt
        // (group has only audio-only actions, or no motion actions). Fall
        // through to InteractionHandler so the user still gets feedback.
        LOG_DEBUG("InstanceSession[{}]: voice-pack group for \"{}\" has no "
                  "motion action → falling back to InteractionHandler",
                  m_instanceId, areaId.toStdString());
    }

    LOG_DEBUG("InstanceSession[{}]: hit area=\"{}\" → InteractionHandler",
              m_instanceId, areaId.toStdString());
    m_interactionHandler.handleHitEvent(env);
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
