// InstallPipelineTest (P5, §B.4 tail): archive install — happy pack (VoicePackScanner
// sentinel discovery e2e), happy model (ModelScanner sentinel discovery e2e),
// zip-slip malicious archives (zero on-disk traces), bad-sha download refused,
// cancel-then-install refused, non-pack layouts, wrong-sentinel-for-target,
// disallowed targetDir, reinstall-replaces (both targets). Fixtures are built
// in-process with miniz (mz_zip_writer) — full control over stored entry
// names, including the hostile ones a real-world writer would normalize away.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <miniz.h>

#include <algorithm>

#include "core/DownloadService.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceManager.hpp"
#include "core/ModelScanner.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/VoicePackScanner.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

using core::DownloadService;
using core::scanAvailableModels;
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
    if (!f.open(QIODevice::WriteOnly)) {
        qFatal("writeFile: cannot open %s", qPrintable(path));
    }
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

// A minimal valid model archive: <Name>/<Name>.model3.json + one placeholder
// texture (ModelScanner's qualification contract — discovery only checks the
// sentinel file's existence, so the JSON body is a plausible stub).
bool buildGoodModelZip(const QString& zipPath, const QString& modelName)
{
    ZipBuilder z(zipPath);
    const QByteArray model3 =
        QByteArray("{\"Version\":3,\"FileReferences\":{\"Textures\":[\"") +
        modelName.toUtf8() + QByteArray("/texture_00.png\"]}}");
    return z.addDir(modelName + QStringLiteral("/")) &&
           z.add(modelName + QStringLiteral("/") + modelName +
                     QStringLiteral(".model3.json"),
                 model3) &&
           z.add(modelName + QStringLiteral("/texture_00.png"),
                 QByteArray("\x89PNG-fixture"));
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
    void testModelInstallE2E();
    void testZipSlipAbsoluteAndParentAndDrive();
    void testZipSlipLeavesZeroTraces();
    void testBadSha256BlocksInstall();
    void testCancelledJobCannotInstall();
    void testNonPackArchiveRejected();
    void testWrongSentinelForTarget();
    void testDisallowedTargetDirRejected();
    void testReinstallReplacesExistingPack();
    void testReinstallReplacesExistingModel();

    // ── v1.13 owner-scoped install (resource isolation, rev2 §3) ───────
    void testOwnerScopedPackInstallLandsInPluginDomain();
    void testOwnerScopedModelInstallLandsInPluginDomain();
    void testOwnerScopedInstallStillValidatesSentinels();

    // ── v1.13 api_version routing gate (rev2 §3.1/§3.5) ─────────────────
    void testDownloadAdapterApiVersionRoutingGate();

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
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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

void InstallPipelineTest::testModelInstallE2E()
{
    // Models-target chain: file:// download → installArchive("models") →
    // ModelScanner discovers the new model via the <Name>/<Name>.model3.json
    // sentinel. The models root is injected same-root as the scanner
    // (<rendererBase>/Resources/Models — the production wiring contract).
    const QString zipPath = m_tmp.filePath(QStringLiteral("haru.zip"));
    QVERIFY2(buildGoodModelZip(zipPath, QStringLiteral("Haru")),
             "fixture build failed");
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-model"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-model")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
    int modelSignals = 0;
    QObject::connect(&service, &DownloadService::modelInstalled,
                     [&modelSignals](const QString&) { ++modelSignals; });
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("haru.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("models");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    QCOMPARE(modelSignals, 1);
    QTRY_COMPARE(listener.finishedPath,
                 QDir(rendererBase).filePath(QStringLiteral("Resources/Models/Haru")));
    QVERIFY(QFile::exists(QDir(listener.finishedPath).filePath(
        QStringLiteral("Haru.model3.json"))));

    // e2e discovery: the scanner (takes the renderer BASE dir) lists it.
    const QStringList models = scanAvailableModels(rendererBase);
    QVERIFY2(models.contains(QStringLiteral("Haru")),
             qPrintable(QStringLiteral("ModelScanner did not discover the "
                                       "installed model; got: %1")
                            .arg(models.join(QStringLiteral(", ")))));
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
                                QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                                QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-mixed")), installRoot,
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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

void InstallPipelineTest::testWrongSentinelForTarget()
{
    // Sentinel predicates are per-target: a voice-pack archive (meta.mko)
    // is NOT a model, and a model archive (model3.json) is NOT a voice
    // pack. Both cross-target installs are InvalidArgument with ZERO
    // residue in the mismatched destination.
    // Case A: pack zip (meta.mko) → "models".
    {
        const QString zipPath = m_tmp.filePath(QStringLiteral("pack-as-model.zip"));
        QVERIFY(buildGoodPackZip(zipPath, QStringLiteral("MisPack")));
        const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-xa"));
        const QString modelsRoot =
            QDir(rendererBase).filePath(QStringLiteral("Resources/Models"));
        DownloadService service(m_tmp.filePath(QStringLiteral("dl-xa")),
                                QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                                modelsRoot);
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("pack-as-model.zip");
        const pet::JobId job = service.start(req, &listener);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

        listener = {};
        pet::InstallSpec spec;
        spec.targetDir = QStringLiteral("models");
        QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
        QVERIFY(listener.errorMessage.contains(QStringLiteral("model sentinel")));
        QCOMPARE(fileCountUnder(modelsRoot), 0);
        QVERIFY(!QDir(modelsRoot).exists(QStringLiteral("MisPack")));
    }
    // Case B: model zip (model3.json) → "voicePacks".
    {
        const QString zipPath = m_tmp.filePath(QStringLiteral("model-as-pack.zip"));
        QVERIFY(buildGoodModelZip(zipPath, QStringLiteral("MisModel")));
        const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-xb"));
        const QString voiceRoot =
            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks"));
        DownloadService service(m_tmp.filePath(QStringLiteral("dl-xb")), voiceRoot,
                                QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("model-as-pack.zip");
        const pet::JobId job = service.start(req, &listener);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

        listener = {};
        pet::InstallSpec spec;
        spec.targetDir = QStringLiteral("voicePacks");
        QCOMPARE(service.installArchive(job, spec), pet::PluginError::InvalidArgument);
        QVERIFY(listener.errorMessage.contains(QStringLiteral("voice-pack sentinel")));
        QCOMPARE(fileCountUnder(voiceRoot), 0);
        QVERIFY(!QDir(voiceRoot).exists(QStringLiteral("MisModel")));
    }
}

void InstallPipelineTest::testDisallowedTargetDirRejected()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("good2.zip"));
    QVERIFY(buildGoodPackZip(zipPath, QStringLiteral("Pack2")));
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-target"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-target")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("good2.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    // The allow-list: exactly two logical targets ("voicePacks" |
    // "models"). Anything else — even a benign path-like value — is an
    // explicit InvalidArgument, never a guess.
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
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
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

void InstallPipelineTest::testReinstallReplacesExistingModel()
{
    // Same WARN + removeRecursively replace semantics as packs — the
    // models target is not special-cased.
    const QString zipPath = m_tmp.filePath(QStringLiteral("haru2.zip"));
    QVERIFY(buildGoodModelZip(zipPath, QStringLiteral("Haru2")));
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-model-reinstall"));
    const QString modelsRoot =
        QDir(rendererBase).filePath(QStringLiteral("Resources/Models"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-model-reinstall")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            modelsRoot);
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("haru2.zip");
    const pet::JobId job = service.start(req, &listener);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("models");
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    // A stale marker proves the SECOND install replaced the tree.
    const QString installDir = QDir(modelsRoot).filePath(QStringLiteral("Haru2"));
    QFile stale(QDir(installDir).filePath(QStringLiteral("stale.txt")));
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.write("stale");
    stale.close();
    listener = {};
    QCOMPARE(service.installArchive(job, spec), pet::PluginError::Ok);
    QVERIFY(listener.finishedJob == job);
    QVERIFY(!QFile::exists(QDir(installDir).filePath(QStringLiteral("stale.txt"))));
    QVERIFY(QFile::exists(QDir(installDir).filePath(
        QStringLiteral("Haru2.model3.json"))));
}

// ────────────────────────────────────────────────────────────────────────────
// v1.13 owner-scoped install (resource isolation, rev2 §3)
// ────────────────────────────────────────────────────────────────────────────

namespace {

// Shared owner-scoped harness tail: install an ALREADY-FINISHED job with a
// non-empty ownerPluginId. The plugin-owner roots are overridden to temp
// dirs by the caller (the setPluginOwnerRootsForTest seam) so nothing ever
// touches the real <dataDir>/PluginData/.
pet::PluginError ownerInstallFinished(DownloadService& service,
                                      pet::JobId job,
                                      const QString& targetDir,
                                      const QString& owner)
{
    pet::InstallSpec spec;
    spec.targetDir = targetDir;
    return service.installArchive(job, spec, owner);
}

} // namespace

// An owner-scoped PACK install lands in the plugin's DATA domain root with
// the VoicePackScanner sentinel layout; the HOST roots stay untouched, and
// the 2-arg host install of the SAME job still lands in the ctor root
// (byte-identical legacy behavior).
void InstallPipelineTest::testOwnerScopedPackInstallLandsInPluginDomain()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("owner-pack.zip"));
    QVERIFY2(buildGoodPackZip(zipPath, QStringLiteral("OwnerPack")),
             "fixture build failed");
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-owner-pack"));
    const QString pluginPacksRoot =
        m_tmp.filePath(QStringLiteral("PluginData/com.example.pet/VoicePacks"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-owner-pack")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
    service.setPluginOwnerRootsForTest(
        m_tmp.filePath(QStringLiteral("PluginData/com.example.pet/Models")),
        pluginPacksRoot);
    int packSignals = 0;
    QObject::connect(&service, &DownloadService::packInstalled,
                     [&packSignals](const QString&) { ++packSignals; });
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("owner-pack.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    QCOMPARE(ownerInstallFinished(service, job, QStringLiteral("voicePacks"),
                                  QStringLiteral("com.example.pet")),
             pet::PluginError::Ok);
    QCOMPARE(packSignals, 1); // post-install signal contract unchanged
    const QString landed =
        QDir(pluginPacksRoot).filePath(QStringLiteral("OwnerPack"));
    QTRY_COMPARE(listener.finishedPath, landed);
    QVERIFY(QFile::exists(QDir(landed).filePath(QStringLiteral("meta.mko"))));

    // Host roots untouched: the scanner over the renderer base sees nothing.
    QVERIFY(scanAvailableVoicePacks(rendererBase).isEmpty());
    // Host-domain install of the SAME job goes to the ctor root.
    pet::InstallSpec hostSpec;
    hostSpec.targetDir = QStringLiteral("voicePacks");
    QCOMPARE(service.installArchive(job, hostSpec), pet::PluginError::Ok);
    QTRY_COMPARE(listener.finishedPath,
                 QDir(rendererBase).filePath(
                     QStringLiteral("Resources/VoicePacks/OwnerPack")));
}

// An owner-scoped MODEL install lands in the plugin's Models/ domain root
// with the ModelScanner sentinel layout; the host scanner sees nothing.
void InstallPipelineTest::testOwnerScopedModelInstallLandsInPluginDomain()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("owner-model.zip"));
    QVERIFY2(buildGoodModelZip(zipPath, QStringLiteral("OwnerModel")),
             "fixture build failed");
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-owner-model"));
    const QString pluginModelsRoot =
        m_tmp.filePath(QStringLiteral("PluginData/com.example.pet/Models"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-owner-model")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks")),
                            QDir(rendererBase).filePath(QStringLiteral("Resources/Models")));
    service.setPluginOwnerRootsForTest(pluginModelsRoot,
        m_tmp.filePath(QStringLiteral("PluginData/com.example.pet/VoicePacks")));
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("owner-model.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    QCOMPARE(ownerInstallFinished(service, job, QStringLiteral("models"),
                                  QStringLiteral("com.example.pet")),
             pet::PluginError::Ok);
    const QString landed =
        QDir(pluginModelsRoot).filePath(QStringLiteral("OwnerModel"));
    QTRY_COMPARE(listener.finishedPath, landed);
    QVERIFY(QFile::exists(QDir(landed).filePath(
        QStringLiteral("OwnerModel.model3.json"))));
    QVERIFY(scanAvailableModels(rendererBase).isEmpty());
}

// The owner redirection does NOT weaken validation: a wrong-sentinel
// archive is rejected with InvalidArgument and leaves the plugin domain
// empty (zero on-disk traces).
void InstallPipelineTest::testOwnerScopedInstallStillValidatesSentinels()
{
    // A MODEL-shaped archive (missing <Pack>/meta.mko) offered to the
    // voicePacks target under an owner.
    const QString zipPath = m_tmp.filePath(QStringLiteral("owner-bad.zip"));
    QVERIFY2(buildGoodModelZip(zipPath, QStringLiteral("WrongShape")),
             "fixture build failed");
    const QString pluginPacksRoot =
        m_tmp.filePath(QStringLiteral("PluginData/com.example.bad/VoicePacks"));
    DownloadService service(m_tmp.filePath(QStringLiteral("dl-owner-bad")),
                            m_tmp.filePath(QStringLiteral("host-packs")),
                            m_tmp.filePath(QStringLiteral("host-models")));
    service.setPluginOwnerRootsForTest(
        m_tmp.filePath(QStringLiteral("PluginData/com.example.bad/Models")),
        pluginPacksRoot);
    RecordingListener listener;
    pet::DownloadRequest req;
    req.url = fileUrl(zipPath);
    req.destName = QStringLiteral("owner-bad.zip");
    const pet::JobId job = service.start(req, &listener);
    QVERIFY(job != 0);
    QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

    QCOMPARE(ownerInstallFinished(service, job, QStringLiteral("voicePacks"),
                                  QStringLiteral("com.example.bad")),
             pet::PluginError::InvalidArgument);
    QVERIFY2(listener.errorMessage.contains(QStringLiteral("sentinel")),
             qPrintable(listener.errorMessage));
    QCOMPARE(fileCountUnder(m_tmp.filePath(
                 QStringLiteral("PluginData/com.example.bad"))),
             0);
}

// ────────────────────────────────────────────────────────────────────────────
// v1.13 api_version routing gate (rev2 §3.1/§3.5): the DownloadApiAdapter
// inside a plugin context routes installArchive by the plugin's MANIFEST
// api_version — ≥ 1.13 → the 3-arg owner-scoped overload (the plugin's
// data domain), < 1.13 → owner "" (the legacy host install roots). The
// manifest api_version reaches the context through the ctor's trailing
// (major, minor) params.
// ────────────────────────────────────────────────────────────────────────────
void InstallPipelineTest::testDownloadAdapterApiVersionRoutingGate()
{
    const QString zipPath = m_tmp.filePath(QStringLiteral("gate-pack.zip"));
    QVERIFY2(buildGoodPackZip(zipPath, QStringLiteral("GatePack")),
             "fixture build failed");
    const QString rendererBase = m_tmp.filePath(QStringLiteral("rb-gate"));
    const QString legacyRoot =
        QDir(rendererBase).filePath(QStringLiteral("Resources/VoicePacks"));
    const QString pluginPacksRoot = m_tmp.filePath(
        QStringLiteral("PluginData/org.gate.route/VoicePacks"));

    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(m_tmp.filePath(QStringLiteral("cfg-gate")), server,
                        pending, [](const PanelConfig&) {});
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl controlApi(&mgr, nullptr);
    const QStringList networkGranted{QStringLiteral("network")};

    DownloadService service(m_tmp.filePath(QStringLiteral("dl-gate")),
                            legacyRoot,
                            QDir(rendererBase).filePath(
                                QStringLiteral("Resources/Models")));
    service.setPluginOwnerRootsForTest(
        m_tmp.filePath(QStringLiteral("PluginData/org.gate.route/Models")),
        pluginPacksRoot);

    auto makeContext = [&](const QString& id, int major, int minor) {
        return std::unique_ptr<core::PluginContextImpl>(
            new core::PluginContextImpl(
                id, &rosterApi, &controlApi, []() { return true; },
                /*pageModel=*/nullptr, /*stream=*/nullptr,
                m_tmp.filePath(QStringLiteral("cfg-gate")), &service,
                networkGranted, /*voicePackRefresh=*/{},
                /*parent=*/nullptr, major, minor));
    };

    pet::InstallSpec spec;
    spec.targetDir = QStringLiteral("voicePacks");

    // ── ≥ 1.13 manifest → owner routing: the install lands in the
    // plugin's data domain; the legacy root stays untouched.
    {
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("gate-pack-new.zip");
        const pet::JobId job = service.start(req, &listener);
        QVERIFY(job != 0);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

        const auto ctx = makeContext(QStringLiteral("org.gate.route"), 1, 13);
        pet::IDownloadApi& dl = ctx->downloadApi();
        QCOMPARE(dl.installArchive(job, spec), pet::PluginError::Ok);
        const QString landed =
            QDir(pluginPacksRoot).filePath(QStringLiteral("GatePack"));
        QVERIFY(QFile::exists(QDir(landed).filePath(QStringLiteral("meta.mko"))));
        QVERIFY(!QFile::exists(
            QDir(legacyRoot).filePath(QStringLiteral("GatePack/meta.mko"))));
    }

    // ── < 1.13 manifest → legacy owner "": the install lands in the
    // service's ctor install root; the plugin domain stays untouched.
    {
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("gate-pack-old.zip");
        const pet::JobId job = service.start(req, &listener);
        QVERIFY(job != 0);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);

        const auto ctx = makeContext(QStringLiteral("org.gate.legacy"), 1, 12);
        pet::IDownloadApi& dl = ctx->downloadApi();
        QCOMPARE(dl.installArchive(job, spec), pet::PluginError::Ok);
        QVERIFY(QFile::exists(QDir(legacyRoot).filePath(
            QStringLiteral("GatePack/meta.mko"))));
        QVERIFY(QDir(pluginPacksRoot).entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot).size() == 1);
    }

    // Sanity: without the "network" capability the adapter still fails
    // loudly with Capability regardless of the api_version.
    {
        RecordingListener listener;
        pet::DownloadRequest req;
        req.url = fileUrl(zipPath);
        req.destName = QStringLiteral("gate-pack-nocap.zip");
        const pet::JobId job = service.start(req, &listener);
        QVERIFY(job != 0);
        QTRY_VERIFY_WITH_TIMEOUT(listener.finishedJob == job, 5000);
        const auto ctx = std::unique_ptr<core::PluginContextImpl>(
            new core::PluginContextImpl(
                QStringLiteral("org.gate.nocap"), &rosterApi, &controlApi,
                []() { return true; }, nullptr, nullptr,
                m_tmp.filePath(QStringLiteral("cfg-gate")), &service,
                QStringList{}, /*voicePackRefresh=*/{},
                /*parent=*/nullptr, 1, 13));
        QCOMPARE(ctx->downloadApi().installArchive(job, spec),
                 pet::PluginError::Capability);
    }
}

QTEST_MAIN(InstallPipelineTest)
#include "InstallPipelineTest.moc"
