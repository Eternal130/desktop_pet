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

// ── TuningApiStub (S5: loud failure, zero silent behavior) ─────────────────

pet::PluginError TuningApiStub::setOpacity(const QString& uuid, double opacity)
{
    Q_UNUSED(opacity);
    LOG_WARN("[plugin-api] tuningApi().setOpacity('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "(§B.4 / plugin_write_enabled) — ERR_CAPABILITY",
             uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::setVolume(const QString& uuid, double volume)
{
    Q_UNUSED(volume);
    LOG_WARN("[plugin-api] tuningApi().setVolume('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::setMuted(const QString& uuid, bool muted)
{
    Q_UNUSED(muted);
    LOG_WARN("[plugin-api] tuningApi().setMuted('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::setFps(const QString& uuid, int fps)
{
    Q_UNUSED(fps);
    LOG_WARN("[plugin-api] tuningApi().setFps('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::playMotion(const QString& uuid,
                                           const QString& group, int index)
{
    Q_UNUSED(group);
    Q_UNUSED(index);
    LOG_WARN("[plugin-api] tuningApi().playMotion('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::setExpression(const QString& uuid,
                                              const QString& expressionId)
{
    Q_UNUSED(expressionId);
    LOG_WARN("[plugin-api] tuningApi().setExpression('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::triggerHitArea(const QString& uuid,
                                               const QString& areaId)
{
    Q_UNUSED(areaId);
    LOG_WARN("[plugin-api] tuningApi().triggerHitArea('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::mountVoicePack(const QString& uuid,
                                               const QString& packId)
{
    Q_UNUSED(packId);
    LOG_WARN("[plugin-api] tuningApi().mountVoicePack('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError TuningApiStub::unmountVoicePack(const QString& uuid)
{
    LOG_WARN("[plugin-api] tuningApi().unmountVoicePack('{}') without the "
             "'instance_tuning' capability or with plugin writes disabled "
             "— ERR_CAPABILITY", uuid.toStdString());
    return pet::PluginError::Capability;
}

// ── SettingsApiStub (S6: loud failure, zero silent behavior) ───────────────

pet::PluginError SettingsApiStub::setCloseAction(const QString& action)
{
    LOG_WARN("[plugin-api] settingsApi().setCloseAction('{}') without the "
             "'settings_write' capability or with plugin writes disabled "
             "(§B.4 / plugin_write_enabled) — ERR_CAPABILITY",
             action.toStdString());
    return pet::PluginError::Capability;
}

pet::PluginError SettingsApiStub::setConfirmOnExit(bool enabled)
{
    Q_UNUSED(enabled);
    LOG_WARN("[plugin-api] settingsApi().setConfirmOnExit() without the "
             "'settings_write' capability or with plugin writes disabled "
             "— ERR_CAPABILITY");
    return pet::PluginError::Capability;
}

pet::PluginError SettingsApiStub::setStartMinimized(bool enabled)
{
    Q_UNUSED(enabled);
    LOG_WARN("[plugin-api] settingsApi().setStartMinimized() without the "
             "'settings_write' capability or with plugin writes disabled "
             "— ERR_CAPABILITY");
    return pet::PluginError::Capability;
}

pet::PluginError SettingsApiStub::setDefaultModelName(const QString& name)
{
    LOG_WARN("[plugin-api] settingsApi().setDefaultModelName('{}') without "
             "the 'settings_write' capability or with plugin writes "
             "disabled — ERR_CAPABILITY", name.toStdString());
    return pet::PluginError::Capability;
}

// ── VoicePackApiImpl (read-only real view; install path is P5) ─────────────

VoicePackApiImpl::VoicePackApiImpl(std::function<void()> refresh, QObject* parent)
    : QObject(parent), m_refresh(std::move(refresh))
{
}

QVector<pet::PackInfo> VoicePackApiImpl::listPacks()
{
    // v1.3 (S6): the host's single scan owner (VoicePackController's
    // cache) wins when wired — the panel page and this API share ONE
    // scan, not two. Standalone fallback below (tests / degraded wiring).
    if (m_packSource) {
        try {
            return m_packSource();
        } catch (...) {
            LOG_ERROR("[plugin-api] pack source threw (isolated) — "
                      "falling back to the standalone scan");
        }
    }
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

void VoicePackApiImpl::subscribePackList(pet::IPackListObserver* observer)
{
    // Same coalescing discipline as InstanceApiImpl's observers.
    if (observer == nullptr || m_packObservers.contains(observer))
        return;
    m_packObservers.append(observer);
}

void VoicePackApiImpl::unsubscribePackList(pet::IPackListObserver* observer)
{
    m_packObservers.removeAll(observer);
}

void VoicePackApiImpl::fanoutPacksChanged()
{
    // Copy + try/catch: an observer that unsubscribes (or throws) inside
    // the callback must not poison the fanout — same §A.1 discipline as
    // InstanceApiImpl::fanoutRosterChanged.
    const auto observers = m_packObservers;
    for (pet::IPackListObserver* observer : observers) {
        try {
            observer->packListChanged();
        } catch (...) {
            LOG_ERROR("[plugin-api] pack-list observer threw in "
                      "packListChanged() — isolated, fanout continues");
        }
    }
}

} // namespace core
