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

// v1.1 (2026-09-21): marker base class for post-freeze interface families.
// Every interface family introduced after the P6a freeze (IInstanceControlApi,
// ISettingsApi, ... — stage-2 roadmap S2) derives from IExtApi and is
// obtained EXCLUSIVELY through IPluginContext::queryApi(). This resolves the
// freeze paradox: "expose a new family via a new IPluginContext accessor"
// would itself be a vtable append, and stage 2 forbids those — so the ONE
// tail append v1.1 spends buys a stable, closed accessor that serves every
// future family. The marker carries no methods on purpose; identity and
// versioning live in the apiId/minVersion pair passed to queryApi().
class IExtApi
{
public:
    virtual ~IExtApi() = default;
};

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

    // v1.1 (2026-09-21) — tail append; the last vtable slot spent under the
    // stage-1 privilege. Lookup channel for post-freeze interface families
    // (IExtApi subclasses). Returns nullptr when apiId is unknown to this
    // host or the host cannot provide at least minVersion of the family —
    // callers MUST handle nullptr (feature-absent, not error). apiId strings
    // and per-family versions are frozen alongside each family as it lands
    // (stage 2, S2); until the first family ships, every query returns
    // nullptr. Dynamic casts on the returned pointer are legal (it is a
    // pet::IExtApi* by contract).
    virtual IExtApi* queryApi(const char* apiId, int minVersion) = 0;
};

} // namespace pet
