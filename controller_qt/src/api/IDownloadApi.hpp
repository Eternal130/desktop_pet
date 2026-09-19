#pragma once

// Plugin SDK — the host download chokepoint (P4, §B.3 + §B.4).
//
// The panel's single legal network path. All methods are asynchronous and
// GUI-thread-delivered; nothing here blocks. Download and install are
// SEPARATE steps by design: "download finished" ≠ "install now" — the user
// confirmation sits between them.
//
// Capability gating (§B.4): when a plugin's manifest does not grant the
// "network" capability, the host hands it a stub whose methods return
// PluginError::Capability — loud failure, no silent no-ops. Plugins WITH
// the grant get the real host DownloadService behind this same interface.

#include "api/PluginTypes.hpp"

namespace pet {

// Download job handle. 0 is never a valid job id.
using JobId = unsigned long long;

// Pure-virtual progress sink (v2 §B.3: no std::function across the
// boundary). All callbacks fire on the GUI thread; keep them quick.
class DownloadListener
{
public:
    virtual ~DownloadListener() = default;

    // Periodic progress. bytesTotal may be 0 when the server sends no
    // Content-Length (indeterminate progress).
    virtual void onProgress(JobId job, long long bytesReceived,
                            long long bytesTotal) = 0;

    // Terminal success: the file passed validation and sits at filePath
    // inside the host staging area. Not yet installed anywhere.
    virtual void onFinished(JobId job, const QString& filePath) = 0;

    // Terminal failure (network error, sha256 mismatch, cancelled job, ...).
    // No further callbacks for this job after onError.
    virtual void onError(JobId job, PluginError code,
                         const QString& message) = 0;
};

class IDownloadApi
{
public:
    virtual ~IDownloadApi() = default;

    // Start fetching request.url into the host staging area under
    // request.destName, streaming to a .part file and hashing on the fly.
    // Returns the new JobId, or 0 when the request is malformed
    // (PluginError::InvalidArgument is also delivered to the listener).
    virtual JobId start(const DownloadRequest& request,
                        DownloadListener* listener) = 0;

    // Best-effort cancel. Terminal onError(Capability/…, "cancelled")
    // semantics are guaranteed by P5; calling cancel on an unknown job is
    // a no-op.
    virtual void cancel(JobId job) = 0;

    // Validate (sha256) → unpack (zip-slip guarded, per-file atomic) →
    // move into spec.targetDir (allow-listed logical destination). Runs
    // only for jobs that finished successfully. Result via the listener's
    // onFinished/onError with the SAME job id.
    virtual PluginError installArchive(JobId job, const InstallSpec& spec) = 0;
};

} // namespace pet
