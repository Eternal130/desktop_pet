#include "core/PluginCapabilityStubs.hpp"

#include <QDir>

#include <spdlog/spdlog.h>

#include "core/MetaMkoParser.hpp"
#include "core/PathResolve.hpp"
#include "core/VoicePackScanner.hpp"
#include "logging/Logging.hpp"

namespace core {

// ── DownloadApiStub (§B.4: loud failure, zero silent behavior) ─────────────

pet::JobId DownloadApiStub::start(const pet::DownloadRequest& request,
                                  pet::DownloadListener* listener)
{
    Q_UNUSED(request);
    LOG_WARN("[plugin-api] downloadApi().start() called before P5 DownloadService "
             "(url='{}') — returning ERR_CAPABILITY",
             request.url.toStdString());
    if (listener != nullptr) {
        listener->onError(0, pet::PluginError::Capability,
                          QStringLiteral("download service not available (P5)"));
    }
    return 0; // never a valid JobId
}

void DownloadApiStub::cancel(pet::JobId job)
{
    Q_UNUSED(job); // no jobs exist before P5 — nothing to cancel
}

pet::PluginError DownloadApiStub::installArchive(pet::JobId job, const pet::InstallSpec& spec)
{
    Q_UNUSED(job);
    Q_UNUSED(spec);
    LOG_WARN("[plugin-api] downloadApi().installArchive() called before P5 — "
             "returning ERR_CAPABILITY");
    return pet::PluginError::Capability;
}

// ── VoicePackApiImpl (read-only real view; install path is P5) ─────────────

VoicePackApiImpl::VoicePackApiImpl(QObject* parent)
    : QObject(parent)
{
}

QVector<pet::PackInfo> VoicePackApiImpl::listPacks()
{
    // Same discovery the VoicePacks page uses: scanner takes the renderer
    // BASE dir and appends Resources/VoicePacks internally (scanner .hpp
    // contract). Missing dir / no packs → empty list, never an error.
    QVector<pet::PackInfo> result;
    const QString baseDir = defaultRendererDir();
    const QStringList dirs = scanAvailableVoicePacks(baseDir);
    for (const QString& name : dirs) {
        pet::PackInfo info;
        info.id = name;
        info.dirPath = QDir(baseDir).filePath(
            QStringLiteral("Resources/VoicePacks/") + name);
        // meta.mko summary when parseable (parser takes the pack DIR);
        // absence degrades to bare names (never-throws, same as the
        // panel's own pack list). No version field in meta.mko — empty.
        const auto parsed = parseMetaMko(info.dirPath);
        if (parsed.has_value()) {
            info.displayName = parsed->displayName;
            info.groupCount = parsed->groups.size();
        } else {
            info.displayName = name;
        }
        result.append(info);
    }
    return result;
}

void VoicePackApiImpl::refreshScan()
{
    // P5 wires this to the VoicePackController refresh the VoicePacks page
    // uses; for now the next listPacks() call re-scans anyway (pure static
    // scanner), which is the honest read-only semantics.
    LOG_DEBUG("[plugin-api] voicePackApi().refreshScan() — re-scan happens on "
              "next listPacks() until P5 wires the controller refresh");
}

QString VoicePackApiImpl::installPath()
{
    return QDir(defaultRendererDir())
        .filePath(QStringLiteral("Resources/VoicePacks"));
}

} // namespace core
