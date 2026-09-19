#pragma once

// Plugin SDK — the one interface every panel plugin implements (P4, §B.3).
//
// Lifecycle (§B.3): the host validates the plugin manifest → calls
// initialize() exactly once, in manifest order → the plugin registers its
// pages / subscribes to rosters / does one-time setup → on panel exit the
// host calls shutdown() in reverse initialization order.
//
// Contracts:
//   - initialize must return within ~2s (it runs on the GUI thread before
//     the first frame settles); anything slower must be deferred to the
//     plugin's own page/QtConcurrent work triggered later
//   - shutdown must return within the host's ≤200ms total budget for ALL
//     plugins; no heavy work, no GUI calls, no new registrations
//   - exceptions must not escape (the host catches everything per §A.1,
//     but an escaping exception marks the plugin Failed)
//   - no method may be called after shutdown returned

#include <QtPlugin>

#include "api/PluginTypes.hpp"

namespace pet {

class IPluginContext;

class IPanelPlugin
{
public:
    virtual ~IPanelPlugin() = default;

    // The single entry point. `context` is owned by the host and stays
    // valid until shutdown() returns; the plugin must NOT store it beyond
    // its own lifetime, delete it, or call it from non-GUI threads.
    // Return PluginError::Ok on success; any other value (or an escaping
    // exception) marks the plugin Failed with the error surfaced in the
    // plugin management UI.
    virtual PluginError initialize(IPluginContext& context) = 0;

    // Reverse-order teardown at panel exit. Best-effort: the host catches
    // exceptions and enforces the time budget; misbehavior here is logged,
    // never fatal.
    virtual void shutdown() = 0;
};

} // namespace pet

// Stage 1 (§A.3) creates plugins through the CMake-generated factory table;
// the iid becomes load-bearing in stage 2 (QPluginLoader metadata check).
// Declared from day one so implementations can already carry
// Q_INTERFACES(pet::IPanelPlugin) and the string is frozen at P6a.
Q_DECLARE_INTERFACE(pet::IPanelPlugin, "org.desktop-pet.PanelPlugin/1.0")
