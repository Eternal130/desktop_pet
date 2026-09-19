// DownloadServiceTest (P5, §B.4 + §D P5 v2 mock seam): the host download
// chokepoint — happy path over file:// (the staging/hash/pipeline are
// scheme-agnostic; QNAM's file backend exercises the same signal flow as
// https), sha256 accept/reject, cancel mid-flight, error injection through
// the reply-factory seam, malformed requests, and concurrent jobs.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

#include "core/DownloadService.hpp"

using core::DownloadService;

namespace {

// ── Listener fake: records terminal events; never throws ──────────────────
struct RecordingListener final : pet::DownloadListener
{
    pet::JobId finishedJob = 0;
    QString finishedPath;
    pet::JobId erroredJob = 0;
    pet::PluginError errorCode = pet::PluginError::Ok;
    QString errorMessage;
    int progressCount = 0;
    qint64 lastReceived = 0;
    qint64 lastTotal = -1;

    void onProgress(pet::JobId, long long received, long long total) override
    {
        ++progressCount;
        lastReceived = received;
        lastTotal = total;
    }
    void onFinished(pet::JobId job, const QString& filePath) override
    {
        finishedJob = job;
        finishedPath = filePath;
    }
    void onError(pet::JobId job, pet::PluginError code, const QString& message) override
    {
        erroredJob = job;
        errorCode = code;
        errorMessage = message;
    }
};

// A listener that throws from every callback — the §A.1 isolation check:
// a misbehaving plugin must not break the service.
struct ThrowingListener final : pet::DownloadListener
{
    void onProgress(pet::JobId, long long, long long) override { throw std::runtime_error("boom"); }
    void onFinished(pet::JobId, const QString&) override { throw std::runtime_error("boom"); }
    void onError(pet::JobId, pet::PluginError, const QString&) override { throw std::runtime_error("boom"); }
};

// ── Fake reply (seam): manual chunk/finish/error control ───────────────────
class FakeReply final : public QNetworkReply
{
    Q_OBJECT
public:
    explicit FakeReply(QObject* parent = nullptr)
        : QNetworkReply(parent)
    {
        open(QIODevice::ReadOnly);
    }
    void enqueue(const QByteArray& chunk)
    {
        m_buffer += chunk;
        emit readyRead();
    }
    void finish()
    {
        emit finished();
    }
    void fail(NetworkError error, const QString& message)
    {
        setError(error, message);
        emit errorOccurred(error);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return m_buffer.size() + QNetworkReply::bytesAvailable();
    }
    void abort() override
    {
        if (m_aborted)
            return;
        m_aborted = true;
        // Real QNAM aborts report OperationCanceledError then finished; the
        // service's Cancelling state rides either ordering.
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        emit errorOccurred(QNetworkReply::OperationCanceledError);
        emit finished();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 n = qMin<qint64>(maxSize, m_buffer.size());
        std::memcpy(data, m_buffer.constData(), static_cast<size_t>(n));
        m_buffer.remove(0, static_cast<int>(n));
        return n;
    }

private:
    QByteArray m_buffer;
    bool m_aborted = false;
};

// ── Seam factories ──────────────────────────────────────────────────────────
class ManualFactory final : public DownloadService::IReplyFactory
{
public:
    // Holds the last created FakeReply so the test can drive it.
    FakeReply* lastReply = nullptr;
    QNetworkReply* createReply(const QUrl& url, QObject*) override
    {
        Q_UNUSED(url);
        lastReply = new FakeReply();
        return lastReply;
    }
};

class FailingFactory final : public DownloadService::IReplyFactory
{
public:
    QNetworkReply* createReply(const QUrl& url, QObject*) override
    {
        auto* reply = new FakeReply();
        // Fail asynchronously so start() has already returned a job id —
        // the realistic error path.
        QMetaObject::invokeMethod(
            reply, [reply]() {
                reply->fail(QNetworkReply::ConnectionRefusedError,
                            QStringLiteral("injected connection refusal"));
            },
            Qt::QueuedConnection);
        return reply;
    }
};

QString writeFileHelper(const QString& dir, const QString& name, const QByteArray& content)
{
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return {};
    return path;
}

} // namespace

class DownloadServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void testFileDownloadHappyPathWithSha256();
    void testSha256MismatchRejected();
    void testCancelMidFlight();
    void testInjectedNetworkError();
    void testMalformedRequestsRejected();
    void testConcurrentJobs();
    void testThrowingListenerIsIsolated();
    void testNonexistentFileUrlFailsGracefully();

private:
    QTemporaryDir m_tmp;
};

void DownloadServiceTest::testFileDownloadHappyPathWithSha256()
{
    const QByteArray payload(4096, 'x');
    const QString src = writeFileHelper(m_tmp.path(), QStringLiteral("pack.zip"), payload);
    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());

    DownloadService service(m_tmp.filePath(QStringLiteral("dl")), m_tmp.filePath(QStringLiteral("vp")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QUrl::fromLocalFile(src).toString();
    req.expectedSha256 = sha;
    req.destName = QStringLiteral("pack.zip");

    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_COMPARE_WITH_TIMEOUT(listener.finishedJob, job, 5000);
    QCOMPARE(listener.erroredJob, pet::JobId(0));
    QVERIFY(!listener.finishedPath.isEmpty());
    QFile staged(listener.finishedPath);
    QVERIFY(staged.open(QIODevice::ReadOnly));
    QCOMPARE(staged.readAll(), payload);
    QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("dl/pack.zip.part"))));
    QCOMPARE(service.stagedFilePath(job), listener.finishedPath);
}

void DownloadServiceTest::testSha256MismatchRejected()
{
    const QByteArray payload("legit content");
    const QString src = writeFileHelper(m_tmp.path(), QStringLiteral("evil.zip"), payload);
    const QString wrongSha = QString(64, QLatin1Char('0'));

    DownloadService service(m_tmp.filePath(QStringLiteral("dl2")), m_tmp.filePath(QStringLiteral("vp2")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QUrl::fromLocalFile(src).toString();
    req.expectedSha256 = wrongSha;
    req.destName = QStringLiteral("evil.zip");

    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Generic);
    QVERIFY(listener.errorMessage.contains(QStringLiteral("sha256 mismatch")));
    // .part and final are both absent — a failed validation leaves NOTHING staged
    QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("dl2/evil.zip.part"))));
    QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("dl2/evil.zip"))));
}

void DownloadServiceTest::testCancelMidFlight()
{
    DownloadService service(m_tmp.filePath(QStringLiteral("dl3")), m_tmp.filePath(QStringLiteral("vp3")));
    ManualFactory factory;
    service.setReplyFactory(&factory);

    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QStringLiteral("https://example.invalid/slow.bin");
    req.destName = QStringLiteral("slow.bin");

    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    factory.lastReply->enqueue(QByteArray(1024, 'a'));
    QTest::qWait(10); // let readyRead land
    QCOMPARE(service.activeJobCount(), 1);

    service.cancel(job);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Cancelled);
    QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("dl3/slow.bin.part"))));
    QCOMPARE(service.activeJobCount(), 0); // cancelled jobs are cleaned up
}

void DownloadServiceTest::testInjectedNetworkError()
{
    DownloadService service(m_tmp.filePath(QStringLiteral("dl4")), m_tmp.filePath(QStringLiteral("vp4")));
    FailingFactory factory;
    service.setReplyFactory(&factory);

    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QStringLiteral("https://example.invalid/down.bin");
    req.destName = QStringLiteral("down.bin");

    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Generic);
    QVERIFY(listener.errorMessage.contains(QStringLiteral("connection refusal")));
    QCOMPARE(service.activeJobCount(), 0);
}

void DownloadServiceTest::testMalformedRequestsRejected()
{
    DownloadService service(m_tmp.filePath(QStringLiteral("dl5")), m_tmp.filePath(QStringLiteral("vp5")));
    RecordingListener listener;

    pet::DownloadRequest badScheme;
    badScheme.url = QStringLiteral("http://insecure.example/x.zip"); // http rejected
    badScheme.destName = QStringLiteral("x.zip");
    QCOMPARE(service.start(badScheme, &listener), pet::JobId(0));
    QCOMPARE(listener.erroredJob, pet::JobId(0));
    QCOMPARE(listener.errorCode, pet::PluginError::InvalidArgument);

    listener = {};
    pet::DownloadRequest badName;
    badName.url = QStringLiteral("https://ok.example/x.zip");
    badName.destName = QStringLiteral("../escape");
    QCOMPARE(service.start(badName, &listener), pet::JobId(0));
    QCOMPARE(listener.errorCode, pet::PluginError::InvalidArgument);

    listener = {};
    pet::DownloadRequest badSha;
    badSha.url = QStringLiteral("https://ok.example/x.zip");
    badSha.destName = QStringLiteral("x.zip");
    badSha.expectedSha256 = QStringLiteral("zz");
    QCOMPARE(service.start(badSha, &listener), pet::JobId(0));
    QCOMPARE(listener.errorCode, pet::PluginError::InvalidArgument);
}

void DownloadServiceTest::testConcurrentJobs()
{
    const QByteArray a(2048, 'a');
    const QByteArray b(1024, 'b');
    const QByteArray c(512, 'c');
    const QString srcA = writeFileHelper(m_tmp.path(), QStringLiteral("a.bin"), a);
    const QString srcB = writeFileHelper(m_tmp.path(), QStringLiteral("b.bin"), b);
    const QString srcC = writeFileHelper(m_tmp.path(), QStringLiteral("c.bin"), c);

    DownloadService service(m_tmp.filePath(QStringLiteral("dl6")), m_tmp.filePath(QStringLiteral("vp6")));
    RecordingListener la, lb, lc;
    auto makeReq = [](const QString& src, const char* name) {
        pet::DownloadRequest r;
        r.url = QUrl::fromLocalFile(src).toString();
        r.destName = QString::fromLatin1(name);
        return r;
    };
    const pet::JobId ja = service.start(makeReq(srcA, "a.bin"), &la);
    const pet::JobId jb = service.start(makeReq(srcB, "b.bin"), &lb);
    const pet::JobId jc = service.start(makeReq(srcC, "c.bin"), &lc);
    QVERIFY(ja != 0 && jb != 0 && jc != 0);
    QCOMPARE(service.activeJobCount(), 3);
    QTRY_VERIFY_WITH_TIMEOUT(la.finishedJob == ja && lb.finishedJob == jb &&
                                 lc.finishedJob == jc, 5000);
    QFile fa(la.finishedPath); QVERIFY(fa.open(QIODevice::ReadOnly)); QCOMPARE(fa.readAll(), a);
    QFile fb(lb.finishedPath); QVERIFY(fb.open(QIODevice::ReadOnly)); QCOMPARE(fb.readAll(), b);
    QFile fc(lc.finishedPath); QVERIFY(fc.open(QIODevice::ReadOnly)); QCOMPARE(fc.readAll(), c);
}

void DownloadServiceTest::testThrowingListenerIsIsolated()
{
    const QByteArray payload("tiny");
    const QString src = writeFileHelper(m_tmp.path(), QStringLiteral("tiny.bin"), payload);

    DownloadService service(m_tmp.filePath(QStringLiteral("dl7")), m_tmp.filePath(QStringLiteral("vp7")));
    ThrowingListener thrower;
    pet::DownloadRequest req;
    req.url = QUrl::fromLocalFile(src).toString();
    req.destName = QStringLiteral("tiny.bin");

    const pet::JobId job = service.start(req, &thrower);
    QVERIFY(job != 0);
    // The service must survive the throwing callbacks and finish the job.
    QTRY_VERIFY_WITH_TIMEOUT(service.stagedFilePath(job).isEmpty() == false, 5000);
    QCOMPARE(service.activeJobCount(), 1); // Finished jobs stay for installArchive
}

void DownloadServiceTest::testNonexistentFileUrlFailsGracefully()
{
    DownloadService service(m_tmp.filePath(QStringLiteral("dl8")), m_tmp.filePath(QStringLiteral("vp8")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QUrl::fromLocalFile(m_tmp.filePath(QStringLiteral("does-not-exist.bin"))).toString();
    req.destName = QStringLiteral("missing.bin");

    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Generic);
    QCOMPARE(service.activeJobCount(), 0);
}

QTEST_MAIN(DownloadServiceTest)
#include "DownloadServiceTest.moc"
