#include "core/PluginCapabilityStubs.hpp"

#include <QDir>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include "core/ConfigDir.hpp"
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

// ── InstanceControlApiStub (S2: loud failure, zero silent behavior) ────────

pet::PluginError InstanceControlApiStub::create(const pet::InstanceSpec& spec,
                                                QString* outUuid)
{
    Q_UNUSED(spec);
    if (outUuid != nullptr)
        outUuid->clear();
    LOG_WARN("[plugin-api] instanceControlApi().create() without the "
             "'instance_lifecycle' capability or with plugin writes disabled "
             "(§B.4 / plugin_write_enabled) — ERR_CAPABILITY");
    return pet::PluginError::Capability;
}

pet::PluginError InstanceControlApiStub::remove(const QString& uuid)
{
    LOG_WARN("[plugin-api] instanceControlApi().remove('{}') without the "
             "'instance_lifecycle' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError InstanceControlApiStub::start(const QString& uuid)
{
    LOG_WARN("[plugin-api] instanceControlApi().start('{}') without the "
             "'instance_lifecycle' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError InstanceControlApiStub::stop(const QString& uuid)
{
    LOG_WARN("[plugin-api] instanceControlApi().stop('{}') without the "
             "'instance_lifecycle' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError InstanceControlApiStub::restart(const QString& uuid)
{
    LOG_WARN("[plugin-api] instanceControlApi().restart('{}') without the "
             "'instance_lifecycle' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError InstanceControlApiStub::loadModel(const QString& uuid,
                                                   const QString& modelName)
{
    LOG_WARN("[plugin-api] instanceControlApi().loadModel('{}', '{}') without "
             "the 'instance_lifecycle' capability or with plugin writes "
             "disabled — ERR_CAPABILITY",
             uuid.toStdString(), modelName.toStdString());
    return pet::PluginError::Capability;
}

// ── VoicePackApiImpl (read-only real view; install path is P5) ─────────────

VoicePackApiImpl::VoicePackApiImpl(std::function<void()> refresh, QObject* parent)
    : QObject(parent), m_refresh(std::move(refresh))
{
}

QVector<pet::PackInfo> VoicePackApiImpl::listPacks()
{
    // Same dual-source discovery the VoicePacks page uses: scanner takes the
    // renderer BASE dir (appends Resources/VoicePacks internally) plus the
    // per-user packs dir (storage-layout revision — downloads install there
    // and win name collisions; scanner .hpp contract). Missing dirs / no
    // packs → empty list, never an error.
    QVector<pet::PackInfo> result;
    const QString baseDir = defaultRendererDir();
    // Scanner contract: entries are ABSOLUTE pack directory paths.
    const QStringList packDirs =
        scanAvailableVoicePacks(baseDir, ConfigDir::userVoicePacksDir());
    for (const QString& dir : packDirs) {
        const QString name = QFileInfo(dir).fileName();
        pet::PackInfo info;
        info.id = name;
        info.dirPath = dir;
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
    // P5: routes into the host's VoicePackController::rescan (wired via
    // PluginHost::setVoicePackRefresh in PanelUiBoot) so the panel's pack
    // list updates right after an install. listPacks() re-scans lazily too.
    if (m_refresh) {
        try {
            m_refresh();
        } catch (...) {
            LOG_ERROR("[plugin-api] voicePack refresh callback threw (isolated)");
        }
        return;
    }
    LOG_DEBUG("[plugin-api] voicePackApi().refreshScan() — no host refresh wired; "
              "re-scan happens on next listPacks()");
}

QString VoicePackApiImpl::installPath()
{
    // Storage-layout revision: installs land in the per-user packs dir
    // (DownloadService installRoot — PanelApplication wiring), never inside
    // the (possibly read-only) install tree.
    return ConfigDir::userVoicePacksDir();
}

} // namespace core
