#include "ui/InstanceControlBridge.hpp"

// S5 host dogfooding — see the header for the dependency discipline
// (api/ + Qt only) and the int-error-code contract.

InstanceControlBridge::InstanceControlBridge(pet::ITuningApi* tuningApi,
                                             pet::IInstanceControlApi* controlApi,
                                             QObject* parent)
    : QObject(parent), m_tuningApi(tuningApi), m_controlApi(controlApi)
{
}

// ── Tuning forwards ─────────────────────────────────────────────────────────
// Every method: null API (degraded wiring) fails loudly with Generic —
// never a crash; results land back on the page through the live
// InstanceSession property NOTIFYs (reads stay on the live object).

int InstanceControlBridge::setOpacity(const QString& uuid, double v)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->setOpacity(uuid, v));
}

int InstanceControlBridge::setVolume(const QString& uuid, double v)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->setVolume(uuid, v));
}

int InstanceControlBridge::setMuted(const QString& uuid, bool muted)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->setMuted(uuid, muted));
}

int InstanceControlBridge::setFps(const QString& uuid, int fps)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->setFps(uuid, fps));
}

int InstanceControlBridge::playMotion(const QString& uuid, const QString& group,
                                      int index)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->playMotion(uuid, group, index));
}

int InstanceControlBridge::setExpression(const QString& uuid,
                                         const QString& exprId)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->setExpression(uuid, exprId));
}

int InstanceControlBridge::triggerHitArea(const QString& uuid,
                                          const QString& areaId)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->triggerHitArea(uuid, areaId));
}

int InstanceControlBridge::mountVoicePack(const QString& uuid,
                                          const QString& packId)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->mountVoicePack(uuid, packId));
}

int InstanceControlBridge::unmountVoicePack(const QString& uuid)
{
    if (m_tuningApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_tuningApi->unmountVoicePack(uuid));
}

// ── Lifecycle forwards ──────────────────────────────────────────────────────

int InstanceControlBridge::start(const QString& uuid)
{
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_controlApi->start(uuid));
}

int InstanceControlBridge::stop(const QString& uuid)
{
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_controlApi->stop(uuid));
}

int InstanceControlBridge::restart(const QString& uuid)
{
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_controlApi->restart(uuid));
}

int InstanceControlBridge::loadModel(const QString& uuid,
                                     const QString& modelName)
{
    if (m_controlApi == nullptr)
        return static_cast<int>(pet::PluginError::Generic);
    return static_cast<int>(m_controlApi->loadModel(uuid, modelName));
}
