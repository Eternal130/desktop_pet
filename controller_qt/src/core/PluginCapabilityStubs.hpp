#pragma once

// Capability stubs (P4, §B.4 stub pattern) — the pre-P5 bodies behind
// IPluginContext::downloadApi() / voicePackApi().
//
// DownloadApiStub: EVERY method fails loudly with PluginError::Capability
// — the honest "not implemented yet / not granted" answer. No silent
// no-ops: a plugin that tries to download before P5 gets a diagnosable
// error surfaced through its own DownloadListener.
//
// VoicePackApiImpl: thin real read-only view (scan via VoicePackScanner +
// PathResolve default renderer dir). refreshScan is host-side P5 wiring
// for now: logged, and listPacks() reflects the current directory state
// on the next call.

#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "api/IDownloadApi.hpp"
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
