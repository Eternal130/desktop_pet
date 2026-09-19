#pragma once

// Plugin SDK — read-only instance roster view (P4, §B.3).
//
// Deliberately READ-ONLY: no command sending, no config writes (YAGNI —
// every method on this interface is a permanent commitment; the panel's own
// UI remains the only writer). Plugins observe; the user acts through the
// panel.

#include "api/PluginTypes.hpp"

namespace pet {

// v2 (§B.3): roster change notifications use this pure-virtual observer,
// NOT std::function — std:: types must not cross the plugin boundary
// (MinGW cross-DLL std ABI traps). The host keeps the pointer; the plugin
// must call unsubscribeRoster before returning from shutdown().
class IRosterObserver
{
public:
    virtual ~IRosterObserver() = default;

    // Fired on the GUI thread after the roster changed (instance created /
    // deleted / status or connection flip). Plugins must return quickly —
    // slow handlers delay every subsequent GUI event. Query instances()
    // inside the callback for the fresh snapshot.
    virtual void rosterChanged() = 0;
};

class IInstanceApi
{
public:
    virtual ~IInstanceApi() = default;

    // Snapshot of every pet instance currently known to the panel, in
    // roster order. Cheap (small list); safe to poll from a page binding.
    virtual QVector<InstanceInfo> instances() = 0;

    // Register/remove a roster-change observer. Duplicate registrations of
    // the same pointer are coalesced; unregistering an unknown pointer is
    // a no-op. All callbacks fire on the GUI thread.
    virtual void subscribeRoster(IRosterObserver* observer) = 0;
    virtual void unsubscribeRoster(IRosterObserver* observer) = 0;
};

} // namespace pet
