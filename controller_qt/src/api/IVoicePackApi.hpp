#pragma once

// Plugin SDK — voice-pack discovery (P4, §B.3).
//
// Read-only view over the panel's voice-pack layer. MOUNTING a pack is
// an instance-tuning write — it lives in ITuningApi (v1.3), reached via
// queryApi and gated by "instance_tuning"; this family only lists packs
// and reacts to scans. The install path belongs to IDownloadApi + the
// host install pipeline (P5). v1.3 adds the IPackListObserver tail
// append (below).

#include "api/PluginTypes.hpp"

namespace pet {

// v1.3 (S6): pack-list change notification — same pure-virtual observer
// discipline as IRosterObserver (no std::function across the boundary,
// GUI-thread callbacks, quick handlers only, unsubscribe before
// shutdown() returns). Fired after a scan observed a different pack set
// (install landed, directory changed out-of-band, host rescan).
class IPackListObserver
{
public:
    virtual ~IPackListObserver() = default;

    // The pack list changed; query listPacks() inside for the fresh
    // snapshot. Coarse-grained by design (no per-pack add/remove events).
    virtual void packListChanged() = 0;
};

class IVoicePackApi
{
public:
    virtual ~IVoicePackApi() = default;

    // Packs currently discovered under <rendererDir>/Resources/VoicePacks.
    // Each entry carries the parsed meta.mko summary. Empty when no packs
    // are deployed — never an error.
    virtual QVector<PackInfo> listPacks() = 0;

    // Ask the host to re-run the pack scan (P5: called by the install
    // pipeline after new packs land; plugins may call it after user action
    // that changes the directory out-of-band). Cheap; results arrive via
    // the next listPacks().
    virtual void refreshScan() = 0;

    // Absolute path of the pack root directory (…/Resources/VoicePacks).
    // Read-only knowledge; writes go through the host install pipeline.
    virtual QString installPath() = 0;

    // v1.3 (2026-09-21, S6) — TAIL APPEND (stage-1 family still evolving
    // pre-P6a; the v1.1 one-time queryApi append privilege is not
    // re-spent here): register/remove the pack-list observer. Duplicate
    // registrations of the same pointer are coalesced; unregistering an
    // unknown pointer is a no-op. All callbacks fire on the GUI thread.
    // There is no initial replay — query listPacks() after subscribing
    // for the current snapshot.
    virtual void subscribePackList(IPackListObserver* observer) = 0;
    virtual void unsubscribePackList(IPackListObserver* observer) = 0;
};

} // namespace pet
