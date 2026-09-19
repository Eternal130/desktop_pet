// InstallPipelineTest (P5, §B.4 tail): archive install — happy pack (VoicePackScanner
// sentinel discovery e2e), zip-slip malicious archives (zero on-disk traces),
// bad-sha download refused, cancel-then-install refused, non-pack layouts,
// disallowed targetDir, reinstall-replaces. Fixtures are built in-process
// with miniz (mz_zip_writer) — full control over stored entry names,
// including the hostile ones a real-world writer would normalize away.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <miniz.h>

#include <algorithm>

#include "core/DownloadService.hpp"
#include "core/VoicePackScanner.hpp"

using core::DownloadService;
using core::scanAvailableVoicePacks;

namespace {

struct RecordingListener final : pet::DownloadListener
{
    pet::JobId finishedJob = 0;
    QString finishedPath;
    pet::JobId erroredJob = 0;
    pet::PluginError errorCode = pet::PluginError::Ok;
    QString errorMessage;
    void onProgress(pet::JobId, long long, long long) override {}
    void onFinished(pet::JobId job, const QString& path) override
    {
        finishedJob = job;
        finishedPath = path;
    }
    void onError(pet::JobId job, pet::PluginError code, const QString& message) override
    {
        erroredJob = job;
        errorCode = code;
        errorMessage = message;
    }
};

// ── Fixture builder: stored (uncompressed) entries, arbitrary names ────────
class ZipBuilder
{
public:
    explicit ZipBuilder(const QString& path)
        : m_path(path)
    {
        mz_zip_zero_struct(&m_zip);
        m_ok = mz_zip_writer_init_file(&m_zip, path.toUtf8().constData(), 0);
    }
    ~ZipBuilder()
    {
        if (m_ok)
            mz_zip_writer_finalize_archive(&m_zip);
        mz_zip_writer_end(&m_zip); // no-op after addHostileName closed it
    }
    ZipBuilder(const ZipBuilder&) = delete;
    ZipBuilder& operator=(const ZipBuilder&) = delete;

    bool add(const QString& name, const QByteArray& content = {})
    {
        return m_ok && mz_zip_writer_add_mem(&m_zip, name.toUtf8().constData(),
                                             content.constData(),
                                             static_cast<size_t>(content.size()),
                                             MZ_NO_COMPRESSION);
    }
    bool addDir(const QString& name)
    {
        return m_ok && mz_zip_writer_add_mem(&m_zip, name.toUtf8().constData(),
                                             nullptr, 0, MZ_NO_COMPRESSION);
    }
    // Hostile-name escape hatch: miniz's writer refuses absolute/illegal
    // names. Build under an equal-length placeholder, finalize, then patch
    // the raw name bytes (local header + central dir; identical length ⇒
    // offsets unchanged, MZ_NO_COMPRESSION ⇒ no stream to desync).
    bool addHostileName(const QString& hostileName, const QByteArray& content = {})
    {
        const QString placeholder =
            QStringLiteral("P") + QString(hostileName.size() - 1, QLatin1Char('z'));
        if (!add(placeholder, content))
            return false;
        if (!mz_zip_writer_finalize_archive(&m_zip))
            return false;
        mz_zip_writer_end(&m_zip);
        m_ok = false; // writer closed; further adds are programmer error
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        QByteArray bytes = f.readAll();
        f.close();
        const QByteArray before = placeholder.toUtf8();
        const QByteArray after = hostileName.toUtf8();
        if (before.size() != after.size() || !bytes.contains(before))
            return false;
        bytes.replace(before, after);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               f.write(bytes) == bytes.size();
    }

    bool ok() const { return m_ok; }

private:
    QString m_path;
    mz_zip_archive m_zip{};
    bool m_ok = false;
};

QString writeFile(const QString& dir, const QString& name, const QByteArray& content)
{
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(content);
    return path;
}

QString fileUrl(const QString& path)
{
    return QUrl::fromLocalFile(path).toString();
}

// A minimal valid voice-pack archive: <Pack>/meta.mko + one ogg.
bool buildGoodPackZip(const QString& zipPath, const QString& packName)
{
    ZipBuilder z(zipPath);
    return z.addDir(packName + QStringLiteral("/")) &&
           z.add(packName + QStringLiteral("/meta.mko"),
                 QByteArray("BUNDLE-fixture")) &&
           z.add(packName + QStringLiteral("/audio/tap.ogg"),
                 QByteArray("\x01vorbis-fixture"));
}

// Counts every regular file under dir (recursively) — the "zero traces"
// assertion helper for zip-slip.
int fileCountUnder(const QString& dir)
{
    int count = 0;
    QDir d(dir);
    const auto entries = d.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& e : entries) {
        if (e.isDir())
            count += fileCountUnder(e.absoluteFilePath());
        else
            ++count;
    }
    return count;
}

} // namespace

class InstallPipelineTest : public QObject
{
    Q_OBJECT

private slots:
    void testDownloadInstallDiscoverE2E();
    void testZipSlipAbsoluteAndParentAndDrive();
    void testZipSlipLeavesZeroTraces();
    void testBadSha256BlocksInstall();
    void testCancelledJobCannotInstall();
    void testNonPackArchiveRejected();
    void testDisallowedTargetDirRejected();
    void testReinstallReplacesExistingPack();

private:
    QTemporaryDir m_tmp;
};

void InstallPipelineTest::testDownloadInstallDiscoverE2E()
{
    // Gate-2 chain: file:// download (sha256-verified) → install → the
    // VoicePackScanner discovers the new pack via the meta.mko sentinel.
    const QString zipPath = m_tmp.filePath(QStringLiteral("good.zip"));
    QVERIFY2(buildGoodPackZip(zipPath, QStringLiteral("FixturePack")),
             "fixture build failed");
    QFile zipFile(zipPath);
    QVERIFY(zipFile.open(QIODevice::ReadOnly));
    const QByteArray zipBytes = zipFile.readAll();
    const QString sha = QString::fromLatin1(
        QCryptographicHash::hash(zipBytes, QCryptographicHash::Sha256).toHex());

    // renderer-like root: <tmp>/Resources/VoicePacks is the install root the
    // scanner derives from the base dir (VoicePackScanner convention).
    const QString rendererBase = m_tmp.filePath(QStringLiteral("renderer"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.expectedSha256 = sha;
    req.destName = QStringLiteral("good.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    QTRY_COMPARE(listener.finishedPath,
                 QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks/FixturePack")));
    QVERIFY(QFile::exists(QDir(listener.finishedPath).filePath(QStringLiteral("meta.mko"))));

    // e2e discovery: the scanner (takes the renderer BASE dir) finds it.
    // Scanner contract: entries are ABSOLUTE pack dir paths.
    const QStringList packs = scanAvailableVoicePacks(rendererBase);
    QVERIFY2(std::any_of(packs.cbegin(), packs.cend(), [](const QString& p) {
                 return p.endsWith(QStringLiteral("/FixturePack"));
             }),
             qPrintable(QStringLiteral("VoicePackScanner did not discover the "
                                       "installed pack; got: %1")
                            .arg(packs.join(QStringLiteral(", ")))));
}

void InstallPipelineTest::testZipSlipAbsoluteAndParentAndDrive()
{
    struct Case { const char* name; const char* label; };
    const Case cases[] = {
        {"../evil.txt", "parent-escape"},
        {"/tmp/evil-abs.txt", "absolute-path"},
        {"C:/evil-drive.txt", "windows-drive"},
        {"Fix/../../evil2.txt", "nested-escape"},
    };
    for (const Case& c : cases) {
        const QString zipPath = m_tmp.filePath(
            QStringLiteral("evil-%1.zip").arg(QLatin1String(c.label)));
        {
            ZipBuilder z(zipPath);
            QVERIFY2(z.ok(), "writer init failed");
            QVERIFY2(z.add(QStringLiteral("Decoy/meta.mko"), QByteArray("mko")),
                     "decoy add failed");
            const QString hostile = QString::fromLatin1(c.name);
            const bool added = hostile.startsWith(QLatin1Char('/')) ||
                                       hostile.at(1) == QLatin1Char(':')
                                   ? z.addHostileName(hostile, QByteArray("payload"))
                                   : z.add(hostile, QByteArray("payload"));
            QVERIFY2(added,
                     qPrintable(QStringLiteral("add failed: %1").arg(hostile)));
        }
        const QString rendererBase = m_tmp.filePath(
            QStringLiteral("rb-") + QLatin1String(c.label));
        DownloadService service(m_tmp.filePath(QStringLiteral("dl-") + QLatin1String(c.label)),
                                QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("evil.zip");
        const pet::JobId job = service.start(req, &listener);
        QVERIFY(job != 0);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

        listener = {};
        pet::InstallSpec spec;
        spec.targetDir = QStringLiteral("voicePacks");
        QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
        QVERIFY(listener.erroredJob == job);
        QVERIFY(listener.errorMessage.contains(QStringLiteral("unsafe entry")));
        // Nothing escaped: no evil files anywhere in the test root.
        QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("evil.txt"))));
        QVERIFY(!QFile::exists(m_tmp.filePath(QStringLiteral("evil2.txt"))));
        QVERIFY(!QFile::exists(QStringLiteral("/tmp/evil-abs.txt")));
        QVERIFY(!QFile::exists(QStringLiteral("C:/evil-drive.txt")));
    }
}

void InstallPipelineTest::testZipSlipLeavesZeroTraces()
{
    // Even the safe entries of a partially-malicious archive must not land:
    // validation runs over ALL entries before ANY byte is written.
    const QString zipPath = m_tmp.filePath(QStringLiteral("mixed.zip"));
    {
        ZipBuilder z(zipPath);
        QVERIFY(z.addDir(QStringLiteral("GoodPack/")));
        QVERIFY(z.add(QStringLiteral("GoodPack/meta.mko"), QByteArray("mko")));
        QVERIFY(z.add(QStringLiteral("GoodPack/innocent.ogg"), QByteArray("ogg")));
        QVERIFY(z.add(QStringLiteral("GoodPack/../../escape.bin"), QByteArray("x")));
    }
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-mixed"));
    const QString installRoot = QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-mixed")), installRoot);
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("mixed.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    listener = {};
    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
    QVERIFY(!QDir(installRoot).exists(QStringLiteral("GoodPack")));
    QCOMPARE(fileCountUnder(rendererBase), 0);
}

void InstallPipelineTest::testBadSha256BlocksInstall()
{
    // A corrupted download never reaches install: sha256 validation fails
    // the job at finish time (§B.4), so installArchive sees no finished job.
    const QString zipPath = m_tmp.filePath(QStringLiteral("corrupt.zip"));
    QVERIFY(buildGoodPackZip(zipPath, QStringLiteral("CorruptPack")));
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-corrupt"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-corrupt")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.expectedSha256 = QString(64, QLatin1Char('7')); // wrong on purpose
    req.destName = QStringLiteral("corrupt.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Generic);

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::NotFound);
    QCOMPARE(fileCountUnder(rendererBase), 0);
}

// namespace-scope: moc cannot process Q_OBJECT classes nested in functions.
class StallReply final : public QNetworkReply
{
    Q_OBJECT
public:
    explicit StallReply(QObject* parent = nullptr)
        : QNetworkReply(parent)
    {
        open(QIODevice::ReadOnly);
    }
    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
        emit errorOccurred(QNetworkReply::OperationCanceledError);
        emit finished();
    }
    qint64 bytesAvailable() const override { return QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char*, qint64) override { return 0; }
};

class StallFactory final : public DownloadService::IReplyFactory
{
public:
    QNetworkReply* lastReply = nullptr;
    QNetworkReply* createReply(const QUrl&, QObject*) override
    {
        lastReply = new StallReply();
        return lastReply;
    }
};

void InstallPipelineTest::testCancelledJobCannotInstall()
{
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-cancel"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-cancel")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    StallFactory factory;
    service.setReplyFactory(&factory);
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = QStringLiteral("https://example.invalid/pack.zip");
    req.destName = QStringLiteral("pack.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    service.cancel(job);
    QTRY_VERIFY_WITH_TIMEOUT(listener.erroredJob == job, 5000);
    QCOMPARE(listener.errorCode, pet::PluginError::Cancelled);

    // The job was cleaned up by the cancel path — installArchive has no
    // listener to call back; the synchronous PluginError carries the verdict.
    listener = {};
    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::NotFound);
    QVERIFY(listener.errorMessage.isEmpty());
    QVERIFY(listener.finishedJob == 0);
}

void InstallPipelineTest::testNonPackArchiveRejected()
{
    // Sentinel discipline: no meta.mko → not a voice pack, rejected even
    // though the archive is structurally fine and safe.
    const QString zipPath = m_tmp.filePath(QStringLiteral("notapack.zip"));
    {
        ZipBuilder z(zipPath);
        QVERIFY(z.addDir(QStringLiteral("SomeDir/")));
        QVERIFY(z.add(QStringLiteral("SomeDir/readme.txt"), QByteArray("hi")));
    }
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-notapack"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-notapack")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("notapack.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    listener = {};
    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
    QVERIFY(listener.errorMessage.contains(QStringLiteral("sentinel")));
    QCOMPARE(fileCountUnder(rendererBase), 0);
}

void InstallPipelineTest::testDisallowedTargetDirRejected()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("good2.zip"));
    QVERIFY(buildGoodPackZip(zipPath, QStringLiteral("Pack2")));
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-target"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-target")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("good2.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    // The P5 allow-list: only "voicePacks". Anything else — even a benign
    // path-like value — is an explicit InvalidArgument, never a guess.
    for (const char* bad : {"plugins", "../escape", "assets", ""}) {
        listener = {};
        pet::InstallSpec spec;
        spec.targetDir = QString::fromLatin1(bad);
        QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
    }
    QCOMPARE(fileCountUnder(rendererBase), 0);
}

void InstallPipelineTest::testReinstallReplacesExistingPack()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("good3.zip"));
    QVERIFY(buildGoodPackZip(zipPath, QStringLiteral("Pack3")));
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-reinstall"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-reinstall")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("good3.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    // A stale sentinel marker proves the SECOND install replaced the tree.
    const QString installDir = QDir(rendererBase)
                                   .filePath(QStringLiteral("Resources/VoicePacks/Pack3"));
    QFile stale(QDir(installDir).filePath(QStringLiteral("stale.txt")));
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write("stale");
    stale.close();
    listener = {};
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    QVERIFY(listener.finishedJob == job);
    QVERIFY(!QFile::exists(QDir(installDir).filePath(QStringLiteral("stale.txt"))));
    QVERIFY(QFile::exists(QDir(installDir).filePath(QStringLiteral("meta.mko"))));
}

QTEST_MAIN(InstallPipelineTest)
#include "InstallPipelineTest.moc"
