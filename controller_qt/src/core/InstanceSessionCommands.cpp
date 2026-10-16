#include "core/InstanceSession.hpp"

#include <QFileInfo>
#include <QJsonObject>

#include <spdlog/spdlog.h>
#include "api/EventIds.hpp"
#include "core/EventCatalog.hpp"
#include "logging/Logging.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/ModelScanner.hpp"
#include "core/PathResolve.hpp"
#include "network/Envelope.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

// InstanceSessionCommands -- Phase-5 todo-6 Q_INVOKABLE helpers extracted to keep
// InstanceSession.cpp under the 250 pure-LOC ceiling. Two groups:
//   1) Model metadata accessors (availableModels / motionGroupNames /
//      motionCount / expressionNames) -- read-only wrappers over m_modelInfo
//      and core::scanAvailableModels so QML never touches std::optional<ModelInfo>
//      or QDir directly.
//   2) Runtime command triggers (playMotion / setExpression) -- build the
//      inline Envelope (Protocol factories for play_motion / set_expression
//      arrive in todo 16) and route via sendCommand, mirroring the setters in
//      InstanceSessionSetters.cpp.
//
// sendCommand lives in InstanceSessionSetters.cpp (declared private in the
// header); the triggers here call it via the linker (standard multi-TU split).

QStringList InstanceSession::availableModels() const
{
    // Prefer m_rendererDir (stored in start()) so the list matches what the
    // running renderer actually sees; fall back to the persisted
    // config.rendererPath, then defaultRendererDir() 鈥?the same fallback
    // chain start() uses, so the ComboBox populates before start().
    QString dir = m_rendererDir;
    if (dir.isEmpty())
        dir = m_config.rendererPath;
    if (dir.isEmpty())
        dir = core::defaultRendererDir();
    return core::scanAvailableModels(dir);
}

QStringList InstanceSession::motionGroupNames() const
{
    if (!m_modelInfo.has_value()) {
        return {};
    }
    return m_modelInfo->motionGroups.keys();
}

int InstanceSession::motionCount(const QString& group) const
{
    if (!m_modelInfo.has_value()) {
        return 0;
    }
    return m_modelInfo->motionGroups.value(group, 0);
}

QStringList InstanceSession::expressionNames() const
{
    if (!m_modelInfo.has_value()) {
        return {};
    }
    return m_modelInfo->expressions;
}

// Design-doc 搂鈶?third stage tab: hit-area names from the parsed
// .model3.json. Empty until model_loaded fires.
QStringList InstanceSession::hitAreaNames() const
{
    if (!m_modelInfo.has_value()) {
        return {};
    }
    return m_modelInfo->hitAreas;
}

// Design-doc 搂鈶?crash-recovery card: live restart-attempt count from the
// owned RestartController (0..5, reset on successful connect).
int InstanceSession::restartAttempts() const
{
    return m_restartController.attempts();
}

void InstanceSession::playMotion(const QString& group, int index)
{
    LOG_INFO("InstanceSession[{}]: playMotion group=\"{}\" index={}",
             m_instanceId, group.toStdString(), index);

    // priority=2 is PriorityNormal per interface.md 搂B.1 (click-triggered).
    // (todo 16: previously inline createCommand; now via typed factory.)
    sendCommand(Protocol::buildPlayMotion(group, index, 2));
}

void InstanceSession::triggerHitArea(const QString& areaId)
{
    LOG_INFO("InstanceSession[{}]: manual trigger of hit area=\"{}\"",
             m_instanceId, areaId.toStdString());
    // Synthesize the same envelope the renderer's `hit` event carries so the
    // decision chain (MountedBehaviorEngine → InteractionHandler) runs
    // identically to a real click on the pet.
    Envelope env;
    env.type = QStringLiteral("event");
    env.action = QStringLiteral("hit");
    env.payload.insert(QStringLiteral("area_id"), areaId);
    handleHitEvent(env);
}

void InstanceSession::setExpression(const QString& expressionId)
{
    LOG_INFO("InstanceSession[{}]: setExpression id=\"{}\"",
             m_instanceId, expressionId.toStdString());

    // (todo 16: previously inline createCommand; now via typed factory.)
    sendCommand(Protocol::buildSetExpression(expressionId));
}

// 鈹€鈹€ Read-only InstanceConfig field accessors (display-only in the param panel;
// persistence lands in todo 7). One-line wrappers over m_config.

QString InstanceSession::dragMode() const
{
    return m_config.dragMode;
}

int InstanceSession::idleIntervalSeconds() const
{
    return m_config.idleInterval;
}

bool InstanceSession::autoStartEnabled() const
{
    return m_config.autoStart;
}

void InstanceSession::setAutoStart(bool enabled)
{
    if (m_config.autoStart == enabled)
        return;
    m_config.autoStart = enabled;
    if (m_configManager.save(m_config)) {
        LOG_INFO("InstanceSession[{}]: autoStart={} persisted",
                 m_instanceId, enabled);
    } else {
        LOG_ERROR("InstanceSession[{}]: failed to persist autoStart={}",
                  m_instanceId, enabled);
    }
}

bool InstanceSession::setGraphicsBackend(const QString& backend)
{
    // v1.10 offline-only selector (loadModel's offline-half discipline):
    // same-value no-op; otherwise persist ALWAYS (start() resolves the
    // renderer EXECUTABLE from m_config.graphicsBackend — without the
    // persist the choice would silently revert on the next launch) and
    // flip the NOTIFY. NO command is sent: a backend switch needs a
    // different renderer binary, so it takes effect on the next start().
    if (m_config.graphicsBackend == backend)
        return true; // accepted no-op: value already current, nothing to save
    LOG_INFO("InstanceSession[{}]: graphicsBackend \"{}\" -> \"{}\"",
             m_instanceId, m_config.graphicsBackend.toStdString(),
             backend.toStdString());
    // Save-first-then-apply (all-or-nothing): save() upserts the WHOLE
    // config row, so persist a mutated copy and only then flip the live
    // snapshot + NOTIFY. A failed save returns false with BOTH the disk
    // row and m_config still on the old backend — the caller (QML combo
    // bounce / plugin Generic) and the next launch then agree, and the
    // failure is REPORTED instead of a silent in-memory-only flip that
    // reverted on the next launch (exp-4 root cause #2).
    InstanceConfig next = m_config;
    next.graphicsBackend = backend;
    if (!m_configManager.save(next)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist graphicsBackend=\"{}\"",
                  m_instanceId, backend.toStdString());
        return false;
    }
    m_config.graphicsBackend = backend;
    emit graphicsBackendChanged();
    return true;
}

bool InstanceSession::setAvatar(const QString& avatar)
{
    // v1.11 avatar write (IInstanceControlApi::setAvatar tail append):
    // same-value no-op; otherwise save-first-then-apply (the exact
    // setGraphicsBackend template above). Validation lives at the API
    // layer (InstanceControlApiImpl: empty → InvalidArgument); the
    // session applies verbatim.
    if (m_config.avatar == avatar)
        return true; // accepted no-op: value already current, nothing to save
    LOG_INFO("InstanceSession[{}]: avatar \"{}\" -> \"{}\"",
             m_instanceId, m_config.avatar.toStdString(),
             avatar.toStdString());
    // Save-first-then-apply (all-or-nothing): persist a mutated copy and
    // only then flip the live snapshot. A failed save returns false with
    // BOTH the disk row and m_config still on the old avatar. NO NOTIFY —
    // the avatar Q_PROPERTY is CONSTANT; the roster-level
    // dataChanged(AvatarRole) emission is InstanceManager::setAvatar's
    // job (the manager knows the row; the session does not).
    InstanceConfig next = m_config;
    next.avatar = avatar;
    if (!m_configManager.save(next)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist avatar=\"{}\"",
                  m_instanceId, avatar.toStdString());
        return false;
    }
    m_config.avatar = avatar;
    return true;
}

// ── Voice-pack mount (todo 21) ───────────────────────────────────────────

bool InstanceSession::mountVoicePack(const QString& packPath,
                                     const QString& scope)
{
    if (packPath.isEmpty()) {
        unmountVoicePack();
        return false;
    }
    auto pack = core::parseMetaMko(packPath);
    if (!pack.has_value()) {
        LOG_WARN("InstanceSession[{}]: mountVoicePack failed to parse \"{}\" "
                 "— leaving current state", m_instanceId,
                 packPath.toStdString());
        return m_mountedPack.has_value();
    }
    m_mountedPack = std::move(*pack);
    m_behaviorEngine.setVoicePack(&*m_mountedPack);
    // v1.3 seam: remember whether the mounted path changes so the new
    // mountedVoicePackChanged NOTIFY fires only on a real transition
    // (re-mounting the same pack re-parses but is not a change).
    const bool changed = m_config.voicePack != packPath;
    m_config.voicePack = packPath;
    // v1.13 (rev2 §3.4): record the pack's resource domain. Bookkeeping
    // only — the ABSOLUTE packPath semantics above are unchanged; an empty
    // scope (every pre-v1.13 caller) keeps the host-domain default.
    m_config.voicePackScope = scope;
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist voice pack",
                  m_instanceId);
    }
    if (changed)
        emit mountedVoicePackChanged();
    // S8a (v1.5): bus publish at the SAME point as the NOTIFY — only on
    // a real mount transition (same `changed` gate). pack_id is the pack
    // DIRECTORY name, the SDK-wide pack identity (a caller-supplied PATH
    // never reaches the payload).
    if (changed)
        publishHostEvent(pet::kEventPackMounted, /*cancellable=*/false,
                         core::makePackPayload(QFileInfo(packPath).fileName()));
    LOG_INFO("InstanceSession[{}]: mounted voice pack \"{}\" ({} groups)",
             m_instanceId, m_mountedPack->displayName.toStdString(),
             m_mountedPack->groups.size());
    // Idle-source switch: a pack with an "idle" group takes priority over
    // the model's Idle group — startIdleScheduler re-evaluates (calling
    // start() on a running scheduler is a clean parameterized restart,
    // Scheduler.hpp). No-op while the model is not loaded yet.
    startIdleScheduler();
    return true;
}

void InstanceSession::unmountVoicePack()
{
    if (!m_mountedPack.has_value() && m_config.voicePack.isEmpty()) {
        return;
    }
    // S8a (v1.5): capture the pack id BEFORE the config field is cleared
    // (the bus payload needs the directory name of what WAS mounted).
    const QString previousPackId =
        QFileInfo(m_config.voicePack).fileName();
    m_behaviorEngine.setVoicePack(nullptr);
    m_mountedPack.reset();
    m_config.voicePack.clear();
    // v1.13: the scope goes with the mount — clearing the pack clears the
    // owning domain too (back to the host-domain default).
    m_config.voicePackScope.clear();
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist voice-pack unmount",
                  m_instanceId);
    }
    // v1.3 seam: this branch only runs when something WAS mounted (the
    // guard above returned otherwise) — always a real transition.
    emit mountedVoicePackChanged();
    // S8a (v1.5): bus publish at the SAME point as the NOTIFY.
    publishHostEvent(pet::kEventPackUnmounted, /*cancellable=*/false,
                     core::makePackPayload(previousPackId));
    LOG_INFO("InstanceSession[{}]: unmounted voice pack", m_instanceId);
    // Idle-source switch back: with the engine cleared, startIdleScheduler
    // falls back to the model's Idle group (or stops when neither source
    // exists). No-op while the model is not loaded yet.
    startIdleScheduler();
}

QString InstanceSession::mountedVoicePack() const
{
    return m_config.voicePack;
}

// getLayout/resetLayout are fire-and-forget from the pending-request
// perspective: sendCommand pushes the envelope and emits commandSent but never
// registers a PendingRequests entry. get_layout's response routes via the
// layout_state EVENT by action (interface.md 搂7.3); reset_layout's Response is
// a plain success ack the controller does not need to await.

void InstanceSession::getLayout()
{
    LOG_INFO("InstanceSession[{}]: getLayout", m_instanceId);
    // No pending request: the reply arrives as a layout_state event routed by
    // action 鈫?handleLayoutStateEvent 鈫?emits layoutUpdated.
    sendCommand(Protocol::buildGetLayout());
}

void InstanceSession::resetLayout()
{
    LOG_INFO("InstanceSession[{}]: resetLayout", m_instanceId);
    // Fire-and-forget: the renderer applies defaults + (if it emits
    // layout_changed) handleLayoutChangedEvent persists the result.
    sendCommand(Protocol::buildResetLayout());
}
