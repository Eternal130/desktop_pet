#include "core/VoicePackScanner.hpp"

#include <algorithm>

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
// (VOICE_PACK_INDEX = "meta.mko") and the layout build.py ships under
// build/bin/Resources/VoicePacks/.
constexpr const char* kResourcesDir = "Resources";
constexpr const char* kVoicePacksSubdir = "VoicePacks";
constexpr const char* kVoicePackIndex = "meta.mko";

} // namespace

QStringList scanAvailableVoicePacks(const QString& rendererDir)
{
    if (rendererDir.isEmpty()) {
        LOG_DEBUG("VoicePackScanner: rendererDir is empty");
        return {};
    }

    const QDir voicePacksDir(QDir(rendererDir).absoluteFilePath(
        QDir::cleanPath(QString::fromLatin1(kResourcesDir) + '/'
                        + QString::fromLatin1(kVoicePacksSubdir))));
    if (!voicePacksDir.exists()) {
        LOG_DEBUG("VoicePackScanner: VoicePacks directory not found: \"{}\"",
                  voicePacksDir.absolutePath().toStdString());
        return {};
    }

    // Dirs only, no . or .. — one subdir per voice pack. Hidden directories
    // (starting with '.') are filtered to match the Java reference, which
    // skips them explicitly. QDir::entryList with QDir::Dirs |
    // QDir::NoDotAndDotDot does NOT filter hidden dirs on its own, so we add
    // QDir::NoSymLinks to avoid following links (parity with Java's
    // Files.isDirectory which follows links — but voice-pack discovery should
    // be filesystem-stable, not symlink-dependent).
    const QStringList subdirs = voicePacksDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot);

    QStringList packs;
    packs.reserve(subdirs.size());
    for (const QString& name : subdirs) {
        // Skip hidden directories (starting with '.') — mirrors Java's
        // `if (name.startsWith(".")) continue;`.
        if (name.startsWith(QLatin1Char('.'))) continue;

        // Qualify: subdir must contain the meta.mko index file.
        // Returns ABSOLUTE pack paths (Resources/VoicePacks/<name>) — the
        // caller feeds each entry straight into parseMetaMko; returning
        // bare dir names made the parser probe build/bin/<name>/meta.mko.
        const QString candidate = QDir(voicePacksDir.absoluteFilePath(name))
                                      .absoluteFilePath(
                                          QString::fromLatin1(kVoicePackIndex));
        if (QFile::exists(candidate)) {
            packs.append(voicePacksDir.absoluteFilePath(name));
        }
    }

    // Case-insensitive sort so the ComboBox is stable across platforms
    // (parity with ModelScanner — Qt's entryList ordering is filesystem-
    // dependent; an explicit sort makes the UI deterministic).
    std::sort(packs.begin(), packs.end(),
              [](const QString& a, const QString& b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });

    LOG_DEBUG("VoicePackScanner: found {} voice packs under \"{}\": {}",
              packs.size(),
              voicePacksDir.absolutePath().toStdString(),
              packs.join(QStringLiteral(", ")).toStdString());
    return packs;
}

} // namespace core
