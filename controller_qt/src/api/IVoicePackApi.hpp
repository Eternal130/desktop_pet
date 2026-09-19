#pragma once

// Plugin SDK — voice-pack discovery (P4, §B.3).
//
// Read-only view over the panel's voice-pack layer. Mounting/unmounting
// stays a panel-side user action; plugins may list packs and react to
// scans. The install path belongs to IDownloadApi + the host install
// pipeline (P5).

#include "api/PluginTypes.hpp"

namespace pet {

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
};

} // namespace pet
