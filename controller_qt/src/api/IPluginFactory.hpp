#pragma once

// Plugin SDK — stage-2 dynamic load root protocol (v1.12, 2026-10-16).
//
// A drop-in plugin module (.so/.dll) exposes ONE root QObject that
// implements this factory interface (via Q_PLUGIN_METADATA + Q_INTERFACES,
// see plugins/sdk-template/SamplePluginFactory.hpp). The host's
// DynamicPluginSource validates the module metadata (iid → manifest →
// api_version → id uniqueness → abi) BEFORE instance(), then casts the
// root here; plugin instances are constructed AND destroyed through the
// factory so both happen inside the plugin module — the host never
// constructs or deletes plugin code directly across the module boundary
// (P6a stage-2 criterion 5: cross-module delete is UB).
//
// ABI discipline: same as every api/ header — pure virtual declarations
// only, no std:: types, no inline function bodies (the defaulted virtual
// destructor carries the standard interface-idiom exemption). The iid
// minor follows kApiMajor (same rule as IPanelPlugin: a future major bump
// mints a new iid and old binaries are refused by the api_version gate).
//
// Stage-1 static plugins do NOT use this interface: the CMake-generated
// factory table constructs them inside the same binary, where host-side
// construction/deletion is safe (PluginRegistry::addStaticPlugin).

#include <QtPlugin>

#include "api/IPanelPlugin.hpp"

// Single source of the factory-root iid spelling (P2-1 single-sourcing,
// 2026-10-16 review round): everything that names this iid — the
// Q_DECLARE_INTERFACE below, plugins' Q_PLUGIN_METADATA(IID ...), and the
// host's DynamicPluginSource metadata gate — includes this macro. A macro,
// not a constant: Q_PLUGIN_METADATA/IID need a literal token, and moc
// expands macros from included headers. Pure spelling single-sourcing —
// no new API surface, no version bump.
#define PET_PLUGIN_FACTORY_IID "org.desktop-pet.PanelPluginFactory/1.0"

namespace pet {

class IPluginFactory
{
public:
    virtual ~IPluginFactory() = default;

    // Construct a plugin instance INSIDE the module. The host calls this
    // only after every metadata gate passed. May return nullptr (the host
    // marks the entry Failed). Exceptions are caught and isolated by the
    // host (§A.1) — they must not escape, and the host never retries.
    virtual IPanelPlugin* create() = 0;

    // Destroy a plugin created by THIS factory, inside the module (P6a
    // criterion 5: the host must never delete across the module boundary).
    // Called exactly once per created instance, after IPanelPlugin::
    // shutdown() returned. nullptr-tolerant by convention.
    virtual void destroy(IPanelPlugin* plugin) = 0;
};

} // namespace pet

// Frozen at stage 2 alongside IPanelPlugin's iid. DynamicPluginSource
// compares PET_PLUGIN_FACTORY_IID against the module metadata's "iid"
// before anything is instantiated — a mismatch is a visible Failed entry,
// never a load.
Q_DECLARE_INTERFACE(pet::IPluginFactory, PET_PLUGIN_FACTORY_IID)
