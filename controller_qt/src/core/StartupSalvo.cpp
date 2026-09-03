#include "core/StartupSalvo.hpp"

#include "network/Protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <spdlog/spdlog.h>

// StartupSalvo (T16) — see header for the full flow narrative.
//
// All command factories live in the Protocol namespace (T9). The salvo is a
// declarative list of (factory call → sendOne) pairs; no branching, no
// per-command logic. This keeps the bootstrap auditable: a reviewer reading
// sendSalvo() sees the EXACT 7 commands in the EXACT order they go on the wire.

namespace {
// Compact serialization shared by every command. Mirrors the WsServer send
// convention (compact JSON, no pretty-printing — minimizes wire bytes).
QString toJson(const Envelope& env) {
    return QString::fromUtf8(
        serialize(env).toJson(QJsonDocument::Compact));
}
} // namespace

StartupSalvo::StartupSalvo(QObject* parent) : QObject(parent) {}

void StartupSalvo::setCommandSender(CommandSender sender) {
    m_sender = std::move(sender);
}

QStringList StartupSalvo::sendSalvo(const InstanceConfigLike& c) {
    if (!m_sender) {
        SPDLOG_WARN("StartupSalvo::sendSalvo invoked with no sender; skipping");
        return {};
    }

    // The 7-command salvo in order (architecture-blueprint.md §6.1).
    // Each entry: build envelope → serialize → send → emit commandSent.
    // set_layout carries `scale` (set_scale stays unused — redundant alias).
    QStringList sent;
    sent.reserve(7);

    sent.append(sendOne(Protocol::buildLoadModel(c.modelName)));
    sent.append(sendOne(Protocol::buildSetPosition(c.windowX, c.windowY)));
    sent.append(sendOne(Protocol::buildSetSize(c.windowWidth, c.windowHeight)));
    sent.append(sendOne(Protocol::buildSetOpacity(c.opacity)));
    sent.append(sendOne(Protocol::buildSetFps(c.targetFps)));
    sent.append(sendOne(Protocol::buildSetVolume(c.volume, c.muted)));
    sent.append(sendOne(Protocol::buildSetLayout(
        c.layoutOffsetX, c.layoutOffsetY, c.layoutScale)));

    SPDLOG_INFO("StartupSalvo: sent {} commands after ready", sent.size());
    return sent;
}

void StartupSalvo::sendSetHitAreas(const QJsonArray& hitAreas) {
    if (!m_sender) {
        SPDLOG_WARN("StartupSalvo::sendSetHitAreas invoked with no sender; skipping");
        return;
    }
    sendOne(Protocol::buildSetHitAreas(hitAreas));
    SPDLOG_INFO("StartupSalvo: sent set_hit_areas (count={}) after model_loaded",
                hitAreas.size());
}

QString StartupSalvo::sendOne(const Envelope& env) {
    const QString json = toJson(env);
    m_sender(json);
    // Emit AFTER the send so listeners (tests) see the action in send order.
    emit commandSent(env.action, json);
    return env.action;
}
