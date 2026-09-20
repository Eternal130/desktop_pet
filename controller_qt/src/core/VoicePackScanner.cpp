#include "core/VoicePackScanner.hpp"

#include <algorithm>
#include <QHash>

#include <QDir>
#include <QFile>

// spdlog MUST be included before logging/Logging.hpp (see PathResolve.cpp /
// ModelScanner.cpp for the same rationale).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

namespace core {

namespace {

// The conventional subdirectory + index-file names under a renderer's bundled
// Resources/ tree. Matches the Java reference VoicePackScanner constants
// (VOICE_PACK_INDEX = "meta.mko") and the layout shipped under
// build/bin/Resources/VoicePacks/.
constexpr const char* kResourcesDir = "Resources";
constexpr const char* kVoicePacksSubdir = "VoicePacks";
constexpr const char* kVoicePackIndex = "meta.mko";

// Scan ONE VoicePacks directory (the dir itself, not a Resources parent):
// every non-hidden direct child directory containing meta.mko is a pack.
// Appends "name -> absolute pack path" entries into `out`; a dir that does
// not exist / holds no pack simply appends nothing. Never throws.
void scanSingleSource(const QString& packsDir, QHash<QString, QString>* out)
{
    const QDir dir(packsDir);
    if (!dir.exists()) {
        LOG_DEBUG("VoicePackScanner: packs directory not found: \"{}\"",
                  dir.absolutePath().toStdString());
        return;
    }

    // Dirs only, no . or .. — one subdir per voice pack. Hidden directories
    // (starting with '.') are filtered to match the Java reference, which
    // skips them explicitly. QDir::entryList with QDir::Dirs |
    // QDir::NoDotAndDotDot does NOT filter hidden dirs on its own, so we add
    // QDir::NoSymLinks to avoid following links (parity with Java's
    // Files.isDirectory which follows links — but voice-pack discovery should
    // be filesystem-stable, not symlink-dependent).
    const QStringList subdirs = dir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& name : subdirs) {
        // Skip hidden directories (starting with '.') — mirrors Java's
        // `if (name.startsWith(".")) continue;`.
        if (name.startsWith(QLatin1Char('.'))) continue;

        // Qualify: subdir must contain the meta.mko index file.
        // Returns ABSOLUTE pack paths (<packsDir>/<name>) — the caller feeds
        // each entry straight into parseMetaMko; returning bare dir names
        // made the parser probe build/bin/<name>/meta.mko (P5 e2e-gate
        // finding, see AGENTS.md).
        const QString candidate = QDir(dir.absoluteFilePath(name))
                                      .absoluteFilePath(
                                          QString::fromLatin1(kVoicePackIndex));
        if (QFile::exists(candidate)) {
            out->insert(name, dir.absoluteFilePath(name));
        }
    }
}

} // namespace

QStringList scanAvailableVoicePacks(const QString& rendererDir,
                                    const QString& userPacksDir)
{
    // name -> absolute pack path. QHash insertion overwrites an existing
    // key, so scanning the built-in source FIRST and the user source SECOND
    // gives the user pack precedence on name collisions (storage-layout
    // revision: a downloaded pack supersedes a same-named bundled one).
    QHash<QString, QString> byName;

    if (!rendererDir.isEmpty()) {
        scanSingleSource(QDir(rendererDir).absoluteFilePath(
                             QDir::cleanPath(
                                 QString::fromLatin1(kResourcesDir) + '/'
                                 + QString::fromLatin1(kVoicePacksSubdir))),
                         &byName);
    } else {
        LOG_DEBUG("VoicePackScanner: rendererDir is empty");
    }

    if (!userPacksDir.isEmpty()) {
        // userPacksDir IS the VoicePacks dir itself (NOT a Resources parent)
        // — ConfigDir::userVoicePacksDir() plugs in directly.
        scanSingleSource(userPacksDir, &byName);
    }

    QStringList packs = byName.values();
    packs.reserve(byName.size());

    // Case-insensitive sort so the ComboBox is stable across platforms
    // (parity with ModelScanner — entryList ordering is filesystem-
    // dependent; an explicit sort makes the UI deterministic).
    std::sort(packs.begin(), packs.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    LOG_DEBUG("VoicePackScanner: found {} voice packs "
              "(renderer=\"{}\" user=\"{}\"): {}",
              packs.size(), rendererDir.toStdString(),
              userPacksDir.toStdString(),
              packs.join(QStringLiteral(", ")).toStdString());
    return packs;
}

QStringList scanAvailableVoicePacks(const QString& rendererDir)
{
    return scanAvailableVoicePacks(rendererDir, QString());
}

} // namespace core
