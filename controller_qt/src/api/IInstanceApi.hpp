#pragma once

// Plugin SDK — read-only instance roster view (P4, §B.3).
//
// v1.1 (2026-09-21) decision record: the 2026-09-19 P6a review conclusion
// "IInstanceApi 维持只读" is OVERTURNED by the architecture revision v3 —
// see docs/refactor/plugin-architecture-and-cmake-migration.md (修订 v3,
// 2026-09-21). Rationale: panel dogfooding wants plugin-side writers, and
// withholding write power from in-tree plugins buys no safety (first-party
// plugins are part of the panel — §A.1).
//
// THIS interface keeps its read-only SHAPE (its v1.0 vtable layout is
// unchanged except the v1.2 stage-1 tail append below); plugin write
// capabilities — instance lifecycle (create/remove/start/stop) and tuning
// (scale/layout/motion) — land in a SEPARATE IInstanceControlApi family
// (stage-2 roadmap S2, shipped 2026-09-21 as API 1.2), exposed via
// IPluginContext::queryApi() and gated by the manifest capabilities
// "instance_lifecycle" / "instance_tuning" (see PluginTypes.hpp).
// The v1.2 tail append (subscribeInstances) is instance-granular state
// OBSERVATION, not mutation — the write path stays in IInstanceControlApi.

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

// v1.2 (2026-09-21, S2): instance-granular observer — roster membership
// changes PLUS full runtime-state snapshots. Same discipline as
// IRosterObserver: pure-virtual (no std::function across the boundary),
// GUI-thread callbacks, quick handlers only, unsubscribe before shutdown()
// returns. Coarse-grained by design: instanceStateChanged carries the WHOLE
// InstanceRuntime on any property flip; observers re-read what they need.
class IInstanceObserver
{
public:
    virtual ~IInstanceObserver() = default;

    // Same semantics as IRosterObserver::rosterChanged (instance created /
    // deleted). Query instances() inside for the fresh snapshot.
    virtual void rosterChanged() = 0;

    // A full snapshot of one instance's observable state, fanned out on the
    // GUI thread whenever any backing property flips (status, connection,
    // model, tuning, mute, ...). Removals are signalled via rosterChanged,
    // never via a terminal state event.
    virtual void instanceStateChanged(const QString& uuid,
                                      const InstanceRuntime& info) = 0;

    // The renderer rejected a model load for this instance (missing model
    // dir, corrupt .model3.json, ...); error carries the renderer's
    // error_message.
    virtual void modelLoadFailed(const QString& uuid, const QString& error) = 0;
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

    // v1.2 (2026-09-21, S2) — TAIL APPEND (the stage-1 vtable-append
    // privilege, consumed with this minor bump): register/remove the
    // instance-granular observer (see IInstanceObserver above). Same
    // coalescing/no-op semantics as the roster pair; all callbacks fire on
    // the GUI thread. A subscribe also delivers rosterChanged semantics —
    // there is no initial replay; query instances() after subscribing for
    // the current snapshot.
    virtual void subscribeInstances(IInstanceObserver* observer) = 0;
    virtual void unsubscribeInstances(IInstanceObserver* observer) = 0;
};

} // namespace pet
