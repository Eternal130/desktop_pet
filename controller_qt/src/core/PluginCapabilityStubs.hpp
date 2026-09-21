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

private:
    std::function<void()> m_refresh;
};

} // namespace core
