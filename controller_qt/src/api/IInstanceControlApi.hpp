#pragma once

// Plugin SDK — instance lifecycle WRITE family (S2, v1.2).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kInstanceControlApiId,
// minVersion). Gated by the manifest capability "instance_lifecycle"
// (kCapabilityInstanceLifecycle) AND the host-global plugin_write_enabled
// kill-switch — a plugin without the grant (or with the switch off) gets a
// capability stub whose every method returns PluginError::Capability
// (§B.4 loud-failure discipline).
//
// Threading/blocking contract: every method runs on the GUI thread and is
// NON-BLOCKING. The synchronous return is "accepted", NOT "completed":
//   - Ok                — the operation was accepted (create/remove of a
//                         stopped instance may already be complete)
//   - NotFound          — no instance with that uuid in the roster
//   - InvalidArgument   — malformed argument (empty label/model, ...)
//   - Busy              — the instance is mid-delete (pending-delete);
//                         racing a start/restart against the deferred
//                         removal is refused
// There are NO per-operation callbacks: final results arrive as
// IInstanceObserver state events (subscribeInstances on IInstanceApi) —
// removal completes via the host's two-phase delete, stop completion via
// the status flip to "stopped" + roster/modelLoaded updates.
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + POD/Qt value types only, no std:: across the boundary,
// no inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the six lifecycle
// ops below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kInstanceControlApiId[] = "pet.instance_control";
inline constexpr int kInstanceControlApiVersion = 1;

class IInstanceControlApi : public IExtApi
{
public:
    // Create a new pet instance from spec. On Ok, *outUuid (when non-null)
    // receives the new instance's uuid — the roster change is observable
    // via IInstanceApi::instances()/subscribeInstances immediately after.
    virtual PluginError create(const InstanceSpec& spec, QString* outUuid) = 0;

    // Remove the instance. A RUNNING renderer takes the host's two-phase
    // path (async graceful stop, real removal in the completion callback);
    // a stopped instance is removed immediately. NotFound when the uuid is
    // not in the roster; a remove already pending merges (Ok, idempotent —
    // the in-flight stop completes it).
    virtual PluginError remove(const QString& uuid) = 0;

    // Launch the instance's renderer (async — status events report
    // progress). Busy while a delete is pending on the instance.
    virtual PluginError start(const QString& uuid) = 0;

    // Initiate the graceful renderer stop (non-blocking; completion via
    // the status/state events — stopFinished is a host-internal signal).
    virtual PluginError stop(const QString& uuid) = 0;

    // Stop + queue relaunch (async). Busy while a delete is pending.
    virtual PluginError restart(const QString& uuid) = 0;

    // Switch the instance's model. Offline (renderer not connected) this
    // persists the choice for the next start — same semantics as the
    // model-library page's switch. modelName must be non-empty.
    virtual PluginError loadModel(const QString& uuid, const QString& modelName) = 0;
};

} // namespace pet
