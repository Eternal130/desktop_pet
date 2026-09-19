#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>

#include <spdlog/spdlog.h>

#include "core/DownloadService.hpp"
#include "core/ZipArchive.hpp"
#include "logging/Logging.hpp"

// installArchive (P5, §B.4 pipeline tail): finished-job zip → temp dir
// (per-file QSaveFile atomic writes) → zip-slip validation BEFORE any byte
// is written (zero-落盘 guarantee for malicious archives) → move to
// Resources/VoicePacks/<name>/ (VoicePackScanner sentinel layout) →
// packInstalled signal (→ VoicePackController::rescan, wired in
// PanelUiBoot). Continuation TU of DownloadService (same class, split for
// the 250-LOC discipline like the InstanceSession* family).

namespace core {

namespace {

// P5 allow-list: the only logical install destination (§B.3 InstallSpec).
// Broadening = an explicit design decision, not an accident.
bool isAllowedTarget(const QString& targetDir)
{
    return targetDir == QStringLiteral("voicePacks");
}

// zip-slip gate (§B.4): cleanPath normalization + containment. Returns an
// empty string when the entry is SAFE (the clean relative path), or a
// non-empty rejection reason. Checks: no absolute paths, no drive letters,
// no '..' components after normalization, no empty/dot paths.
QString safeRelativePath(const QString& storedPath)
{
    if (storedPath.isEmpty())
        return QStringLiteral("empty entry name");
    if (storedPath.startsWith(QLatin1Char('/')) || storedPath.startsWith(QLatin1Char('\\')))
        return QStringLiteral("absolute path entry");
    if (storedPath.size() >= 2 && storedPath.at(1) == QLatin1Char(':') &&
        storedPath.at(0).isLetter())
        return QStringLiteral("drive-letter entry");
    const QString clean = QDir::cleanPath(storedPath);
    if (clean == QLatin1String(".") || clean.isEmpty())
        return QStringLiteral("empty entry name");
    const QStringList parts = clean.split(QLatin1Char('/'));
    if (parts.contains(QLatin1String("..")))
        return QStringLiteral("'..' escape attempt");
    if (parts.contains(QLatin1String(".")))
        return QStringLiteral("'.' path component");
    return {};
}

} // namespace

pet::PluginError DownloadService::installArchive(pet::JobId job,
                                                 const pet::InstallSpec& spec)
{
    Job* j = m_jobs.value(job, nullptr);
    const auto reject = [&j](pet::PluginError code, const QString& msg) {
        LOG_ERROR("DownloadService: install rejected — {}", msg.toStdString());
        if (j != nullptr && j->listener != nullptr) {
            try {
                j->listener->onError(j->id, code, msg);
            } catch (...) {
                LOG_ERROR("DownloadService: listener threw in install onError (isolated)");
            }
        }
        return code;
    };

    if (j == nullptr || j->state != Job::State::Finished)
        return reject(pet::PluginError::NotFound,
                      QStringLiteral("job %1 is not a finished download").arg(job));
    if (!isAllowedTarget(spec.targetDir))
        return reject(pet::PluginError::InvalidArgument,
                      QStringLiteral("targetDir '%1' is not allow-listed "
                                     "(P5: voicePacks only)").arg(spec.targetDir));

    ZipArchive zip(j->stagedPath);
    if (!zip.isOpen())
        return reject(pet::PluginError::Generic,
                      QStringLiteral("archive unreadable: %1").arg(zip.errorString()));

    // ── Pass 1: validate EVERY entry before writing a single byte ────────
    // (zip-slip hard gate; malicious archives leave zero traces on disk)
    const QVector<ZipEntry> entries = zip.entries();
    if (entries.isEmpty())
        return reject(pet::PluginError::Generic,
                      QStringLiteral("archive has no entries"));
    for (const ZipEntry& e : entries) {
        // miniz surfaces symlinks as entries whose stored name we reject by
        // content policy here (Unix mode bits are not exposed portably; the
        // extract path writes REGULAR FILES ONLY via QSaveFile, so a link
        // entry can never materialize as a link — belt and suspenders).
        const QString reason = safeRelativePath(e.path);
        if (!reason.isEmpty())
            return reject(pet::PluginError::InvalidArgument,
                          QStringLiteral("unsafe entry '%1': %2").arg(e.path, reason));
    }

    // ── Sentinel layout check: exactly one top-level dir containing
    // meta.mko (VoicePackScanner's discovery contract) ───────────────────
    QSet<QString> topLevelDirs;
    for (const ZipEntry& e : entries) {
        const QStringList parts = QDir::cleanPath(e.path).split(QLatin1Char('/'));
        if (parts.isEmpty() || parts.first().isEmpty())
            continue;
        topLevelDirs.insert(parts.first());
    }
    if (topLevelDirs.size() != 1)
        return reject(pet::PluginError::InvalidArgument,
                      QStringLiteral("archive must contain exactly ONE top-level "
                                     "pack directory (found %1)")
                          .arg(topLevelDirs.size()));
    const QString packName = *topLevelDirs.begin();
    const QString sentinel = packName + QStringLiteral("/meta.mko");
    bool hasSentinel = false;
    for (const ZipEntry& e : entries) {
        if (QDir::cleanPath(e.path) == sentinel)
            hasSentinel = true;
    }
    if (!hasSentinel)
        return reject(pet::PluginError::InvalidArgument,
                      QStringLiteral("missing voice-pack sentinel '%1'").arg(sentinel));

    // ── Pass 2: extract to a temp dir, per-file QSaveFile atomic writes ──
    QTemporaryDir extractTmp;
    if (!extractTmp.isValid())
        return reject(pet::PluginError::Generic,
                      QStringLiteral("cannot create extraction temp dir"));
    for (const ZipEntry& e : entries) {
        if (e.isDir)
            continue;
        const QByteArray data = zip.fileData(e.path);
        if (data.isEmpty() && e.uncompressedSize > 0)
            return reject(pet::PluginError::Generic,
                          QStringLiteral("entry '%1' failed to decompress").arg(e.path));
        const QString dest = QDir(extractTmp.path()).filePath(
            QDir::cleanPath(e.path));
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()))
            return reject(pet::PluginError::Generic,
                          QStringLiteral("cannot create dir for '%1'").arg(e.path));
        QSaveFile f(dest);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            f.write(data) != data.size() || !f.commit()) {
            return reject(pet::PluginError::Generic,
                          QStringLiteral("atomic write failed for '%1'").arg(e.path));
        }
    }

    // ── Final move: <installRoot>/<packName>/ (replace existing, warned) ─
    const QString src = QDir(extractTmp.path()).filePath(packName);
    const QString dst = QDir(m_installRoot).filePath(packName);
    if (QFileInfo::exists(dst)) {
        LOG_WARN("DownloadService: pack '{}' already installed — replacing",
                 packName.toStdString());
        QDir(dst).removeRecursively();
    }
    if (!QDir().mkpath(m_installRoot) || !QDir().rename(src, dst))
        return reject(pet::PluginError::Generic,
                      QStringLiteral("cannot move pack into '%1'").arg(m_installRoot));

    LOG_INFO("DownloadService: installed voice pack '{}' → '{}'",
             packName.toStdString(), dst.toStdString());
    emit packInstalled(packName);
    if (j->listener != nullptr) {
        try {
            j->listener->onFinished(j->id, dst);
        } catch (...) {
            LOG_ERROR("DownloadService: listener threw in install onFinished (isolated)");
        }
    }
    // Job stays Finished/staged — re-install after a manual delete stays
    // possible without re-downloading (download ≠ install, §B.3).
    return pet::PluginError::Ok;
}

} // namespace core
