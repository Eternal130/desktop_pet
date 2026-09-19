#pragma once

// Plugin SDK — the host-provided service context passed to
// IPanelPlugin::initialize() (P4, §B.3).
//
// One context exists per plugin; every accessor returns a host-owned
// reference that stays valid for the plugin's lifetime (until shutdown()
// returns). The context itself carries no state beyond the plugin identity.

#include "api/PluginTypes.hpp"

namespace pet {

class IInstanceApi;
class IVoicePackApi;
class IUiApi;
class IDownloadApi;

class IPluginContext
{
public:
    virtual ~IPluginContext() = default;

    // Read-only roster view + change subscription. Always available.
    virtual IInstanceApi& instanceApi() = 0;

    // Voice-pack discovery. Always available (listPacks may be empty when
    // no renderer resources are deployed).
    virtual IVoicePackApi& voicePackApi() = 0;

    // Page + bubble registration. Always available.
    virtual IUiApi& uiApi() = 0;

    // Network download service — the ONLY legal network path in the panel
    // (§B.4). When the plugin manifest does not grant the "network"
    // capability this returns a capability stub whose every method fails
    // with PluginError::Capability (no silent behavior differences).
    virtual IDownloadApi& downloadApi() = 0;

    // Plugin-private writable directory <configDir>/plugins/<id>/. The host
    // creates it on demand. Write convention (v2, SDK README): JSON +
    // QSaveFile atomic writes + snake_case keys — same rules as the panel's
    // own config layer.
    virtual QString pluginConfigDir() = 0;

    // Route through the host spdlog sink; every line is prefixed with the
    // plugin id automatically. Cheap and non-blocking.
    virtual void log(PluginLogLevel level, const QString& message) = 0;
};

} // namespace pet
