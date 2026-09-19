#include "core/PluginBridge.hpp"

#include <spdlog/spdlog.h>

#include "core/PluginHost.hpp"
#include "logging/Logging.hpp"

namespace core {

PluginBridge::PluginBridge(const QString& pluginId, const QString& title, QObject* parent)
    : QObject(parent), m_pluginId(pluginId), m_title(title)
{
}

void PluginBridge::notify(const QString& text, int durationMs)
{
    // Routed through the host so P5 capability/audit rules have one seam;
    // the host tolerates a missing notification stream (never-throws).
    PluginHost::bridgeNotify(m_pluginId, m_title, text, durationMs);
}

void PluginBridge::log(const QString& message)
{
    LOG_INFO("[plugin/{}] {}", m_pluginId.toStdString(), message.toStdString());
}

} // namespace core
