#pragma once

// Plugin SDK — per-instance layout family (v1.8).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kLayoutApiId, minVersion).
// Gated by the manifest capability "instance_tuning"
// (kCapabilityInstanceTuning — layout ops are the same tuning domain as
// setOpacity / mountVoicePack) AND the host-global plugin_write_enabled
// kill-switch, exactly like pet.instance_tuning / pet.settings: a
// plugin without the grant (or with the switch off, or with no shared
// implementation wired) gets a capability stub whose every method
// returns PluginError::Capability (§B.4 loud-failure discipline).
// minVersion > this family's version → queryApi returns nullptr
// ("feature absent", never an error).
//
// Threading/blocking contract: every method runs on the GUI thread and
// is NON-BLOCKING. The synchronous return is "accepted", NOT
// "completed":
//   - Ok                — the operation was applied to / read from the
//                         session's config snapshot
//   - NotFound          — no instance with that uuid in the roster
//   - Busy              — the instance is mid-delete (pending-delete);
//                         operating on a doomed instance is refused
//   - InvalidArgument   — out pointer null where a value is required
//
// getLayout semantics = CONFIG SNAPSHOT, not live renderer state: it
// projects the session's persisted InstanceConfig layout halves
// (window x/y/width/height + the model layout scale — see LayoutInfo in
// PluginTypes.hpp). Drag-in-progress / pre-persist runtime changes are
// NOT visible here; observe the pet.instance.layout_changed /
// pet.instance.window_resized bus events for the live stream (post-
// commit, per docs/bus-events.md).
//
// resetLayout acceptance semantics: forwards to the session's existing
// reset_layout command (fire-and-forget — the renderer applies its
// defaults; any resulting layout_changed event is persisted + published
// by the session's normal event path). Ok means ACCEPTED, not applied.
//
// setAutoStart acceptance semantics: persists the start-with-panel flag
// into the instance config (the same InstanceSession::setAutoStart the
// detail page's toggle drives); the flag applies at the next panel
// launch — no renderer process is started synchronously. Idempotent on
// the same value.
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the three ops
// below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kLayoutApiId[] = "pet.layout";
inline constexpr int kLayoutApiVersion = 1;

class ILayoutApi : public IExtApi
{
public:
    // Read the last PERSISTED layout snapshot (config state, not live
    // renderer state — see the header block comment). Projects the
    // InstanceConfig window geometry + layout scale into *out. `out`
    // must be non-null (null → InvalidArgument); on any error *out is
    // left zero-initialized, never half-written.
    virtual PluginError getLayout(const QString& uuid, LayoutInfo* out) = 0;

    // Reset the renderer-side layout to defaults (reset_layout command,
    // fire-and-forget; a resulting layout_changed persists + publishes
    // through the session's existing event path).
    virtual PluginError resetLayout(const QString& uuid) = 0;

    // Persist the start-with-panel flag (applies at the next panel
    // launch; no synchronous renderer launch).
    virtual PluginError setAutoStart(const QString& uuid, bool enabled) = 0;
};

} // namespace pet
