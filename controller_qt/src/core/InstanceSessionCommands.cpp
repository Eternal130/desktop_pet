#include "core/InstanceSession.hpp"

#include <QJsonObject>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "core/MetaMkoParser.hpp"
#include "core/ModelScanner.hpp"
#include "core/PathResolve.hpp"
#include "core/SubtitlePresets.hpp"
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

// 鈹€鈹€ Phase-5 Wave 8 todo 21 (subtitle UI helpers) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Mirror of Java MainWindowController subtitleStyleCombo/subtitleAdjustCheck
// handlers. Each setter: update m_config 鈫?send Protocol command via
// sendCommand 鈫?persist via m_configManager. No NOTIFY signal: the QML
// ComboBox/CheckBox read the getter once at panel construction and manage
// their own checked-state afterward (same pattern as dragMode/idleInterval/
// autoStart accessors above).

QStringList InstanceSession::subtitlePresetNames() const
{
    return SubtitlePresets::presetNames();
}

QString InstanceSession::subtitleStylePreset() const
{
    return m_config.subtitleStylePreset;
}

void InstanceSession::setSubtitleStylePreset(const QString& presetName)
{
    if (m_config.subtitleStylePreset == presetName)
        return;
    LOG_INFO("InstanceSession[{}]: setSubtitleStylePreset \"{}\"",
             m_instanceId, presetName.toStdString());
    m_config.subtitleStylePreset = presetName;
    sendCommand(Protocol::buildSetSubtitleStyle(
        SubtitlePresets::mapPresetToStyle(presetName)));
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist subtitle preset", m_instanceId);
    }
}

bool InstanceSession::subtitleAdjustModeEnabled() const
{
    return m_config.subtitleAdjustMode;
}

void InstanceSession::setSubtitleAdjustMode(bool enabled)
{
    if (m_config.subtitleAdjustMode == enabled)
        return;
    LOG_INFO("InstanceSession[{}]: setSubtitleAdjustMode {}",
             m_instanceId, enabled);
    m_config.subtitleAdjustMode = enabled;
    sendCommand(Protocol::buildSetSubtitleAdjustMode(enabled));
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist subtitle adjust mode", m_instanceId);
    }
}

// 鈹€鈹€ Phase-5 Wave 8 todo 22 (layout command triggers) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// ── Voice-pack mount (todo 21) ───────────────────────────────────────────

bool InstanceSession::mountVoicePack(const QString& packPath)
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
    m_config.voicePack = packPath;
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist voice pack",
                  m_instanceId);
    }
    LOG_INFO("InstanceSession[{}]: mounted voice pack \"{}\" ({} groups)",
             m_instanceId, m_mountedPack->displayName.toStdString(),
             m_mountedPack->groups.size());
    return true;
}

void InstanceSession::unmountVoicePack()
{
    if (!m_mountedPack.has_value() && m_config.voicePack.isEmpty()) {
        return;
    }
    m_behaviorEngine.setVoicePack(nullptr);
    m_mountedPack.reset();
    m_config.voicePack.clear();
    if (!m_configManager.save(m_config)) {
        LOG_ERROR("InstanceSession[{}]: failed to persist voice-pack unmount",
                  m_instanceId);
    }
    LOG_INFO("InstanceSession[{}]: unmounted voice pack", m_instanceId);
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
