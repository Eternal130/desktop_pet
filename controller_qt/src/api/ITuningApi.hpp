#pragma once

// Plugin SDK — per-instance runtime tuning WRITE family (S5, v1.3).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kTuningApiId, minVersion).
// Gated by the manifest capability "instance_tuning"
// (kCapabilityInstanceTuning) AND the host-global plugin_write_enabled
// kill-switch — a plugin without the grant (or with the switch off) gets
// a capability stub whose every method returns PluginError::Capability
// (§B.4 loud-failure discipline).
//
// Threading/blocking contract: every method runs on the GUI thread and is
// NON-BLOCKING. The synchronous return is "accepted", NOT "completed":
//   - Ok                — the operation was forwarded to the instance
//                         (the runtime value follows via the renderer
//                         applying it; observers see IInstanceObserver
//                         state events on the property NOTIFY)
//   - NotFound          — no instance with that uuid in the roster
//   - InvalidArgument   — malformed argument (empty packId, ...)
//   - Busy              — the instance is mid-delete (pending-delete);
//                         tuning a doomed instance is refused
// There are NO per-operation callbacks: value changes are observable via
// IInstanceApi::subscribeInstances (InstanceRuntime snapshots).
//
// mountVoicePack packId semantics: the pack DIRECTORY NAME (the SDK-wide
// pack identity, e.g. "pack_v1") — NOT a path. The host resolves it
// against its own pack scan (built-in + user sources) into the absolute
// directory; an unknown name returns NotFound (a caller-supplied path is
// never interpreted, preventing arbitrary-path injection into the
// behavior engine).
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies).

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the nine tuning
// ops below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kTuningApiId[] = "pet.instance_tuning";
inline constexpr int kTuningApiVersion = 1;

class ITuningApi : public IExtApi
{
public:
    // Window opacity, 0.1–1.0 (renderer clamps out-of-range values).
    virtual PluginError setOpacity(const QString& uuid, double opacity) = 0;

    // Audio volume, 0.0–1.0 (renderer clamps out-of-range values).
    virtual PluginError setVolume(const QString& uuid, double volume) = 0;

    // Mute/unmute (independent of the volume value).
    virtual PluginError setMuted(const QString& uuid, bool muted) = 0;

    // Target FPS (0 = adaptive). Same semantics as the detail page's
    // FPS segmented control.
    virtual PluginError setFps(const QString& uuid, int fps) = 0;

    // Play one motion by group + index (renderer silently ignores
    // unknown group/index).
    virtual PluginError playMotion(const QString& uuid, const QString& group,
                                   int index) = 0;

    // Apply an expression by id (renderer silently ignores unknown ids).
    virtual PluginError setExpression(const QString& uuid,
                                      const QString& expressionId) = 0;

    // Manually trigger a hit area — runs the SAME decision chain a real
    // click does (mounted voice-pack behavior first, then the default
    // hit→motion handler).
    virtual PluginError triggerHitArea(const QString& uuid,
                                       const QString& areaId) = 0;

    // Mount the voice pack identified by its DIRECTORY NAME (see the
    // header block comment). Unknown name → NotFound; empty name →
    // InvalidArgument; an unparseable meta.mko → Generic (the current
    // mount state is left untouched).
    virtual PluginError mountVoicePack(const QString& uuid,
                                       const QString& packId) = 0;

    // Unmount the instance's voice pack (hit handling falls back to the
    // default handler) and persist the empty choice.
    virtual PluginError unmountVoicePack(const QString& uuid) = 0;
};

} // namespace pet
