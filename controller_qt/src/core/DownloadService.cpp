#include "core/DownloadService.hpp"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <spdlog/spdlog.h>

#include <QCryptographicHash>

#include "logging/Logging.hpp"

// §B.4 audit note: the QNetworkAccessManager below is the PROCESS-WIDE
// single legal instantiation. Never add another — downloads from ANY
// subsystem go through DownloadService.

namespace core {

namespace {

// ── Production reply factory: owns the one QNetworkAccessManager ──────────
class QnamReplyFactory final : public DownloadService::IReplyFactory
{
public:
    QNetworkReply* createReply(const QUrl& url, QObject* parent) override
    {
        Q_UNUSED(parent); // reply parents itself to the QNAM; service uses deleteLater
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        return m_qnam.get(request);
    }

private:
    QNetworkAccessManager m_qnam; // THE process-wide instance (§B.4)
};

QString normalizedScheme(const QUrl& url)
{
    return url.scheme().toLower();
}

} // namespace

DownloadService::DownloadService(const QString& stagingDir, const QString& installRoot,
                                 QObject* parent)
    : QObject(parent), m_stagingDir(stagingDir), m_installRoot(installRoot)
{
    m_ownedFactory = std::make_unique<QnamReplyFactory>();
    m_factory = m_ownedFactory.get();
    QDir().mkpath(m_stagingDir);
    QDir().mkpath(m_installRoot);
}

DownloadService::~DownloadService()
{
    // Exit path: silent teardown — abort replies, drop .part files, NO
    // listener callbacks (their owners may already be mid-destruction)
    // and no event-loop dependence. Never blocks.
    const auto jobs = m_jobs;
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        it.value()->state = Job::State::Failed; // suppress callbacks below
        if (it.value()->reply != nullptr)
            it.value()->reply->abort();
        cleanupJob(it.key(), true);
    }
}

void DownloadService::setReplyFactory(IReplyFactory* factory)
{
    m_factory = factory;
}

bool DownloadService::validateRequest(const pet::DownloadRequest& request, QString* error)
{
    const QUrl url(request.url);
    const QString scheme = normalizedScheme(url);
    if (!url.isValid() || (scheme != QStringLiteral("https") &&
                           scheme != QStringLiteral("file"))) {
        if (error)
            *error = QStringLiteral("url must be https:// or file:// (got '%1')")
                         .arg(scheme);
        return false;
    }
    if (request.destName.isEmpty() || request.destName.contains(QLatin1Char('/')) ||
        request.destName.contains(QLatin1Char('\\')) ||
        request.destName == QLatin1String(".") || request.destName == QLatin1String("..")) {
        if (error)
            *error = QStringLiteral("destName must be a single path segment");
        return false;
    }
    const QString sha = request.expectedSha256;
    if (!sha.isEmpty()) {
        const QString lower = sha.toLower();
        if (lower.size() != 64 ||
            !std::all_of(lower.cbegin(), lower.cend(), [](QChar c) {
                return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                       (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
            })) {
            if (error)
                *error = QStringLiteral("expectedSha256 must be 64 hex chars or empty");
            return false;
        }
    }
    return true;
}

QString DownloadService::partPathFor(const QString& destName) const
{
    return QDir(m_stagingDir).filePath(destName + QStringLiteral(".part"));
}

pet::JobId DownloadService::start(const pet::DownloadRequest& request,
                                  pet::DownloadListener* listener)
{
    QString validationError;
    if (!validateRequest(request, &validationError)) {
        LOG_WARN("DownloadService: rejected request (destName='{}'): {}",
                 request.destName.toStdString(), validationError.toStdString());
        if (listener != nullptr) {
            try {
                listener->onError(0, pet::PluginError::InvalidArgument, validationError);
            } catch (...) {
                LOG_ERROR("DownloadService: listener threw in onError (isolated)");
            }
        }
        return 0;
    }

    QNetworkReply* reply = nullptr;
    try {
        reply = m_factory->createReply(QUrl(request.url), this);
    } catch (...) {
        LOG_ERROR("DownloadService: reply factory threw (isolated)");
        if (listener != nullptr) {
            try {
                listener->onError(0, pet::PluginError::Generic,
                                  QStringLiteral("reply factory failed"));
            } catch (...) {
            }
        }
        return 0;
    }
    if (reply == nullptr) {
        if (listener != nullptr) {
            try {
                listener->onError(0, pet::PluginError::Generic,
                                  QStringLiteral("no reply created"));
            } catch (...) {
            }
        }
        return 0;
    }

    auto* job = new Job;
    job->request = request;
    job->listener = listener;
    job->reply = reply;
    job->hash = new QCryptographicHash(QCryptographicHash::Sha256);
    job->partFile = new QFile(partPathFor(request.destName));
    if (!job->partFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOG_ERROR("DownloadService: cannot open .part for '{}' — job rejected",
                  request.destName.toStdString());
        reply->deleteLater();
        delete job->hash;
        delete job->partFile;
        delete job;
        if (listener != nullptr) {
            try {
                listener->onError(0, pet::PluginError::Generic,
                                  QStringLiteral("cannot open staging file"));
            } catch (...) {
            }
        }
        return 0;
    }

    const pet::JobId id = m_nextJobId++;
    job->id = id;
    m_jobs.insert(id, job);
    const qint64 expected = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    job->bytesTotal = expected;

    connect(reply, &QNetworkReply::readyRead, this, [this, id]() { handleReadyRead(id); });
    connect(reply, &QNetworkReply::finished, this, [this, id]() { handleFinished(id); });
    connect(reply, &QNetworkReply::errorOccurred, this, [this, id]() { handleError(id); });

    LOG_INFO("DownloadService: job {} started ({} → '{}.part')",
             id, request.url.toStdString(), request.destName.toStdString());
    return id;
}

void DownloadService::cancel(pet::JobId job)
{
    Job* j = m_jobs.value(job, nullptr);
    if (j == nullptr || j->state == Job::State::Finished || j->state == Job::State::Failed) {
        LOG_WARN("DownloadService: cancel for unknown/inactive job {}", job);
        return;
    }
    j->state = Job::State::Cancelling;
    LOG_INFO("DownloadService: cancelling job {}", job);
    j->reply->abort(); // triggers errorOccurred/finished → Cancelled path
}

int DownloadService::activeJobCount() const
{
    return m_jobs.size();
}

QString DownloadService::stagedFilePath(pet::JobId job) const
{
    Job* j = m_jobs.value(job, nullptr);
    return (j != nullptr && j->state == Job::State::Finished) ? j->stagedPath : QString();
}

void DownloadService::handleReadyRead(pet::JobId id)
{
    Job* j = m_jobs.value(id, nullptr);
    if (j == nullptr || j->state != Job::State::Downloading)
        return;
    const QByteArray chunk = j->reply->readAll();
    if (chunk.isEmpty())
        return;
    if (j->partFile->write(chunk) != chunk.size()) {
        notifyError(*j, pet::PluginError::Generic,
                    QStringLiteral("staging write failed"));
        j->state = Job::State::Failed;
        j->reply->abort();
        cleanupJob(id, true);
        return;
    }
    j->hash->addData(chunk);
    j->bytesReceived += chunk.size();
    if (j->listener != nullptr) {
        try {
            j->listener->onProgress(id, j->bytesReceived, j->bytesTotal);
        } catch (...) {
            LOG_ERROR("DownloadService: listener threw in onProgress (isolated)");
        }
    }
}

void DownloadService::handleError(pet::JobId id)
{
    Job* j = m_jobs.value(id, nullptr);
    if (j == nullptr || j->state == Job::State::Finished || j->state == Job::State::Failed)
        return;
    if (j->state == Job::State::Cancelling) {
        // finished() will follow; the Cancelled report happens there (both
        // real QNAM and fake-reply orderings are handled).
        return;
    }
    const QString msg = j->reply->errorString();
    j->state = Job::State::Failed;
    notifyError(*j, pet::PluginError::Generic,
                QStringLiteral("network error: %1").arg(msg));
    cleanupJob(id, true);
}

void DownloadService::handleFinished(pet::JobId id)
{
    Job* j = m_jobs.value(id, nullptr);
    if (j == nullptr || j->state == Job::State::Finished || j->state == Job::State::Failed)
        return;
    // Drain any tail bytes the backend buffered past the last readyRead.
    if (j->state == Job::State::Downloading) {
        const QByteArray tail = j->reply->readAll();
        if (!tail.isEmpty()) {
            j->partFile->write(tail);
            j->hash->addData(tail);
            j->bytesReceived += tail.size();
        }
    }
    const bool cancelled = (j->state == Job::State::Cancelling);
    j->partFile->flush();
    j->partFile->close();
    j->reply->deleteLater();
    j->reply = nullptr;

    if (cancelled) {
        j->state = Job::State::Failed;
        notifyError(*j, pet::PluginError::Cancelled,
                    QStringLiteral("cancelled by caller"));
        QFile::remove(partPathFor(j->request.destName));
        LOG_INFO("DownloadService: job {} cancelled", id);
        cleanupJob(id, false);
        return;
    }
    // sha256 validation (§B.4): non-empty expected must match exactly.
    if (!j->request.expectedSha256.isEmpty()) {
        const QString actual =
            QString::fromLatin1(j->hash->result().toHex()).toLower();
        if (actual != j->request.expectedSha256.toLower()) {
            j->state = Job::State::Failed;
            notifyError(*j, pet::PluginError::Generic,
                        QStringLiteral("sha256 mismatch (expected %1, got %2)")
                            .arg(j->request.expectedSha256, actual));
            QFile::remove(partPathFor(j->request.destName));
            LOG_ERROR("DownloadService: job {} sha256 MISMATCH — .part removed", id);
            cleanupJob(id, false);
            return;
        }
    }
    // Promote .part → final staged name (atomic-ish rename on one fs).
    const QString finalPath = QDir(m_stagingDir).filePath(j->request.destName);
    QFile::remove(finalPath);
    if (!QFile::rename(partPathFor(j->request.destName), finalPath)) {
        j->state = Job::State::Failed;
        notifyError(*j, pet::PluginError::Generic,
                    QStringLiteral("cannot finalize staged file"));
        cleanupJob(id, true);
        return;
    }
    j->stagedPath = finalPath;
    j->state = Job::State::Finished;
    LOG_INFO("DownloadService: job {} finished ({} bytes → '{}')",
             id, j->bytesReceived, finalPath.toStdString());
    if (j->listener != nullptr) {
        try {
            j->listener->onFinished(id, finalPath);
        } catch (...) {
            LOG_ERROR("DownloadService: listener threw in onFinished (isolated)");
        }
    }
    // Job entry STAYS (Finished state) — installArchive(job) needs the
    // staged path + guard against re-install.
}

void DownloadService::notifyError(Job& job, pet::PluginError code, const QString& message)
{
    LOG_WARN("DownloadService: job {} error ({}) — {}",
             job.id, static_cast<int>(code), message.toStdString());
    if (job.listener != nullptr) {
        try {
            job.listener->onError(job.id, code, message);
        } catch (...) {
            LOG_ERROR("DownloadService: listener threw in onError (isolated)");
        }
    }
}

void DownloadService::cleanupJob(pet::JobId id, bool removePart)
{
    Job* j = m_jobs.value(id, nullptr);
    if (j == nullptr)
        return;
    if (removePart && !j->request.destName.isEmpty())
        QFile::remove(partPathFor(j->request.destName));
    if (j->reply != nullptr) {
        j->reply->deleteLater();
        j->reply = nullptr;
    }
    if (j->partFile != nullptr) {
        if (j->partFile->isOpen())
            j->partFile->close();
        delete j->partFile;
        j->partFile = nullptr;
    }
    delete j->hash;
    m_jobs.remove(id);
    delete j;
}

} // namespace core
