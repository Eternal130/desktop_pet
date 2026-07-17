#include "core/InstanceSession.hpp"

#include <QJsonDocument>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "network/Protocol.hpp"
#include "network/WsServer.hpp"

// InstanceSessionSetters — the Q_INVOKABLE runtime command wrappers extracted
// from InstanceSession.cpp to keep the orchestrator's main implementation under
// the 250 pure-LOC ceiling. Each setter persists the value to m_config, emits
// the Q_PROPERTY NOTIFY signal, then sends the corresponding Protocol command
// via sendCommand. sendCommand itself lives here because it is the shared
// serialize→send→emit helper every outbound command (setters + lifecycle) calls.

void InstanceSession::sendCommand(const Envelope& env)
{
    const QByteArray json = serialize(env).toJson(QJsonDocument::Compact);
    m_server.sendText(m_instanceId, QString::fromUtf8(json));
    emit commandSent(env.action);
}

void InstanceSession::setOpacity(double opacity)
{
    if (qFuzzyCompare(m_config.opacity, opacity))
        return;
    m_config.opacity = opacity;
    emit opacityChanged();
    sendCommand(Protocol::buildSetOpacity(opacity));
}

void InstanceSession::setVolume(double volume)
{
    if (qFuzzyCompare(m_config.volume, volume))
        return;
    m_config.volume = volume;
    emit volumeChanged();
    sendCommand(Protocol::buildSetVolume(volume, m_config.muted));
}

void InstanceSession::setMuted(bool muted)
{
    if (m_config.muted == muted)
        return;
    m_config.muted = muted;
    emit mutedChanged();
    sendCommand(Protocol::buildSetVolume(m_config.volume, muted));
}

void InstanceSession::setFps(int fps)
{
    if (m_config.targetFps == fps)
        return;
    m_config.targetFps = fps;
    emit targetFpsChanged();
    sendCommand(Protocol::buildSetFps(fps));
}
