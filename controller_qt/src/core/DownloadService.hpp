#pragma once

// DownloadService (P5, §B.4) — the host download chokepoint: the ONLY legal
// network path in the whole process. One QNetworkAccessManager lives inside
// the production reply factory; nothing else may instantiate one (audit
// gate: grep QNetworkAccessManager → exactly one site).
//
// Pipeline (§B.4): url → streamed .part staging + on-the-fly sha256 →
// validation → (install step, P5 commit 2) → unzip with zip-slip guard →
// per-file atomic writes → move to Resources/VoicePacks/<name>/ → sentinel
// → packInstalled signal (PanelUiBoot wires VoicePackController::rescan).
//
// Contract:
//   - never-throws; every failure is a listener onError (or a synchronous
//     PluginError from installArchive)
//   - GUI thread, fully async signal-driven; listener callbacks fire on the
//     GUI thread and are wrapped in try/catch ALL — a throwing plugin
//     callback never poisons the host (§A.1 discipline)
//   - cancel(): aborts the transfer, deletes the .part, onError(Cancelled)
//   - destructor: silently cancels active jobs WITHOUT callbacks (objects
//     may already be dying — the exit path must not block)
//
// Test seam (§D P5 v2): the reply factory is injectable; production wraps
// the real QNAM, tests inject fake QNetworkReply subclasses for offline /
// error-injection / cancel-mid-flight paths.

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

#include "api/IDownloadApi.hpp"

// Qt classes below are GLOBAL-namespace (mixed-namespace codebase).
class QNetworkReply;
class QFile;
class QCryptographicHash;

namespace core {

class DownloadService : public QObject
{
    Q_OBJECT

public:
    // Reply factory seam. createReply must return a freshly-created reply
    // wired for readyRead/finished/errorOccurred; the service takes Qt
    // ownership management from there (deleteLater on completion).
    class IReplyFactory
    {
    public:
        virtual ~IReplyFactory() = default;
        virtual QNetworkReply* createReply(const QUrl& url, QObject* parent) = 0;
    };

    // stagingDir: where .part files land (default wiring: <configDir>/downloads).
    // installRoot: the concrete Resources/VoicePacks directory for installs
    // (default wiring: PathResolve-derived; tests inject temp dirs).
    explicit DownloadService(const QString& stagingDir, const QString& installRoot,
                             QObject* parent = nullptr);
    ~DownloadService() override;

    // Replaces the production QNAM factory (not owned; must outlive the
    // service or be reset). Used by tests only.
    void setReplyFactory(IReplyFactory* factory);

    // ── pet::IDownloadApi-compatible surface (adapted by DownloadApiImpl) ──
    // Returns 0 (and a synchronous onError(InvalidArgument)) for malformed
    // requests: non-https/file scheme, destName with path separators, or a
    // non-64-hex expectedSha256.
    pet::JobId start(const pet::DownloadRequest& request,
                     pet::DownloadListener* listener);
    void cancel(pet::JobId job);

    // Commit 2 (src/core/DownloadServiceInstall.cpp): finished-job zip
    // install with zip-slip guard. Kept on this class because the API
    // surface is one service (§B.4 chokepoint).
    pet::PluginError installArchive(pet::JobId job, const pet::InstallSpec& spec);

    QString installRoot() const { return m_installRoot; }
    int activeJobCount() const;

    // Test/inspection: staging path of a finished job (empty when unknown).
    QString stagedFilePath(pet::JobId job) const;

signals:
    // Fired after a successful installArchive move; PanelUiBoot wires this
    // to VoicePackController::rescan (§B.4 sentinel → refreshScan chain).
    void packInstalled(const QString& packName);

private:
    friend struct DownloadServiceDetail; // TU-internal transfer/install hooks

    struct Job
    {
        pet::JobId id = 0; // self-knowing: callbacks need it without hash lookups
        pet::DownloadRequest request;
        pet::DownloadListener* listener = nullptr; // not owned
        QNetworkReply* reply = nullptr;    // not owned (deleteLater)
        QFile* partFile = nullptr;         // owned, open while active
        QCryptographicHash* hash = nullptr; // owned
        qint64 bytesReceived = 0;
        qint64 bytesTotal = -1;
        enum class State { Downloading, Cancelling, Finished, Failed } state =
            State::Downloading;
        QString stagedPath; // set on success
    };

    void handleReadyRead(pet::JobId id);
    void handleFinished(pet::JobId id);
    void handleError(pet::JobId id);
    void cleanupJob(pet::JobId id, bool removePart);
    void notifyError(Job& job, pet::PluginError code, const QString& message);
    static bool validateRequest(const pet::DownloadRequest& request, QString* error);
    QString partPathFor(const QString& destName) const;

    QString m_stagingDir;
    QString m_installRoot;
    std::unique_ptr<IReplyFactory> m_ownedFactory; // production QNAM wrapper
    IReplyFactory* m_factory = nullptr;            // points at owned or injected
    QHash<pet::JobId, Job*> m_jobs;                // owned; removed on completion
    pet::JobId m_nextJobId = 1;
};

} // namespace core
