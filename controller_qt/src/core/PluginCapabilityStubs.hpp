#pragma once

// Capability stubs (§B.4 stub pattern) — the no-capability bodies behind
// IPluginContext::downloadApi() / voicePackApi().
//
// DownloadApiStub: EVERY method fails loudly with PluginError::Capability
// — the honest "capability not granted" answer. No silent no-ops: a
// plugin that tries to download without the "network" capability gets a
// diagnosable error surfaced through its own DownloadListener.
//
// VoicePackApiImpl: thin real read-only view (scan via VoicePackScanner +
// PathResolve default renderer dir). refreshScan() triggers the host
// rescan seam; listPacks() reflects the current directory state on the
// next call.

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "api/IDownloadApi.hpp"
#include "api/IInstanceControlApi.hpp"
#include "api/ISettingsApi.hpp"
#include "api/ITuningApi.hpp"
#include "api/IVoicePackApi.hpp"

namespace core {

class DownloadApiStub : public pet::IDownloadApi
{
public:
    ~DownloadApiStub() override = default;

    pet::JobId start(const pet::DownloadRequest& request,
                     pet::DownloadListener* listener) override;
    void cancel(pet::JobId job) override;
    pet::PluginError installArchive(pet::JobId job, const pet::InstallSpec& spec) override;
};

// S2 (v1.2): IInstanceControlApi capability stub — handed out when the
// plugin manifest lacks "instance_lifecycle" OR the host-global
// plugin_write_enabled switch is off. Same loud-failure discipline as
// DownloadApiStub: every method returns PluginError::Capability, no silent
// no-ops. Stateless by design (one instance can serve any number of
// contexts; the context owns it).
class InstanceControlApiStub : public pet::IInstanceControlApi
{
public:
    ~InstanceControlApiStub() override = default;

    pet::PluginError create(const pet::InstanceSpec& spec, QString* outUuid) override;
    pet::PluginError remove(const QString& uuid) override;
    pet::PluginError start(const QString& uuid) override;
    pet::PluginError stop(const QString& uuid) override;
    pet::PluginError restart(const QString& uuid) override;
    pet::PluginError loadModel(const QString& uuid, const QString& modelName) override;
};

// S5 (v1.3): ITuningApi capability stub — handed out when the manifest
// lacks "instance_tuning" OR the write switch is off OR the shared impl
// is not wired. Same loud-failure discipline: every method returns
// PluginError::Capability. Stateless.
class TuningApiStub : public pet::ITuningApi
{
public:
    ~TuningApiStub() override = default;

    pet::PluginError setOpacity(const QString& uuid, double opacity) override;
    pet::PluginError setVolume(const QString& uuid, double volume) override;
    pet::PluginError setMuted(const QString& uuid, bool muted) override;
    pet::PluginError setFps(const QString& uuid, int fps) override;
    pet::PluginError playMotion(const QString& uuid, const QString& group,
                                int index) override;
    pet::PluginError setExpression(const QString& uuid,
                                   const QString& expressionId) override;
    pet::PluginError triggerHitArea(const QString& uuid,
                                    const QString& areaId) override;
    pet::PluginError mountVoicePack(const QString& uuid,
                                    const QString& packId) override;
    pet::PluginError unmountVoicePack(const QString& uuid) override;
};

// S6 (v1.3): ISettingsApi capability stub — handed out when the manifest
// lacks "settings_write" OR the write switch is off OR the shared impl
// is not wired. Every method returns PluginError::Capability. Stateless.
class SettingsApiStub : public pet::ISettingsApi
{
public:
    ~SettingsApiStub() override = default;

    pet::PluginError setCloseAction(const QString& action) override;
    pet::PluginError setConfirmOnExit(bool enabled) override;
    pet::PluginError setStartMinimized(bool enabled) override;
    pet::PluginError setDefaultModelName(const QString& name) override;
};

// VoicePackApiImpl: real read-only view. v1.3 (S6) adds the pack-list
// observer family + the host-scan source seam:
//   - setPackSource: when the host (PanelUiBoot) installs a source that
//     returns the CURRENT pack list from its single scan owner
//     (VoicePackController's cache), listPacks() reads it instead of
//     re-scanning — the panel page and the plugin API share ONE scan,
//     not two. Without a source (tests, degraded wiring) the standalone
//     dual-source scan below still applies.
//   - fanoutPacksChanged(): host wiring calls this when the pack list
//     changed (VoicePackController::packsChanged) → IPackListObserver
//     fanout, same copy-isolate/try-catch discipline as InstanceApiImpl.
class VoicePackApiImpl : public QObject, public pet::IVoicePackApi
{
    Q_OBJECT
public:
    // refresh: P5 host seam — invoked by refreshScan() so the panel's pack
    // list (VoicePackController) re-scans after installs. Empty = log-only
    // (pre-P5 wiring / tests); listPacks() always re-scans regardless.
    explicit VoicePackApiImpl(std::function<void()> refresh = {},
                              QObject* parent = nullptr);
    ~VoicePackApiImpl() override = default;

    QVector<pet::PackInfo> listPacks() override;
    void refreshScan() override;
    QString installPath() override;
    void subscribePackList(pet::IPackListObserver* observer) override;
    void unsubscribePackList(pet::IPackListObserver* observer) override;

    // ── v1.3 host seams (core↔app; std::function never crosses the
    // plugin boundary) ────────────────────────────────────────────────
    void setRefresh(std::function<void()> refresh) { m_refresh = std::move(refresh); }
    void setPackSource(std::function<QVector<pet::PackInfo>()> source)
    { m_packSource = std::move(source); }
    // Fan packListChanged() to every subscriber (host calls this when its
    // single scan observed a change).
    void fanoutPacksChanged();

private:
    std::function<void()> m_refresh;
    std::function<QVector<pet::PackInfo>()> m_packSource; // v1.3 host scan seam
    QVector<pet::IPackListObserver*> m_packObservers;
};

} // namespace core
