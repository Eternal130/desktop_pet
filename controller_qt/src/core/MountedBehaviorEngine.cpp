// MountedBehaviorEngine (Phase 5 Wave 8 todo 20) — C++ port of Java's
// `controller/.../core/MountedBehaviorEngine.java` (246 LOC).
//
// When a voice pack is mounted on an InstanceSession, this engine has PRIORITY
// over InteractionHandler for hit events: if the voice pack defines a group
// for the hit area, the engine builds a play_motion_ext command (motion +
// audio + lipSync + subtitle side-channels). Otherwise the hit falls through
// to InteractionHandler's play_motion path.
//
// ── OGG duration heuristic ──────────────────────────────────────────────────
//
// estimateOggDurationMs parses the OGG container directly via a ~50 LOC
// heuristic: find the \x01vorbis codec-id header → extract sample_rate (uint32
// LE at +12); scan the last 64KB for the final OggS page → read granule_pos
// (int64 LE at +6); duration_ms = granule_pos / sample_rate * 1000.
//
// Alternative considered: libvorbis (precise decode-based duration). Rejected
// here to keep deps minimal — voice-pack audio is short (<30s typically), the
// granule-position approach is exact for finished encodes (the encoder writes
// the final granule as the last sample index), and pulling libvorbis into
// controller_qt would tax every clean build. The heuristic is the same one
// the Java reference uses (MountedBehaviorEngine.java:195-245), so wire-format
// parity is guaranteed. If precise seek-table-based duration is ever needed
// (e.g. for streaming), swap in libvorbis's ov_time_total here.

#include "core/MountedBehaviorEngine.hpp"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"
#include "network/Protocol.hpp"

namespace core {
namespace {

// Locate the group for `areaId` in `voicePack.groups`, case-insensitively.
// Returns nullptr when no match. We walk the QMap rather than use QMap::find
// (which is case-sensitive) because InteractionHandler's lookup convention is
// case-insensitive (InteractionHandler::findHitAction's 3-tier fold) — we want
// the same "head/Head/HEAD all resolve" semantics here.
const VoicePackGroup* findGroupCaseInsensitive(
    const VoicePackInfo* voicePack, const QString& areaId)
{
    if (voicePack == nullptr) return nullptr;
    for (auto it = voicePack->groups.constBegin();
         it != voicePack->groups.constEnd(); ++it) {
        if (it.key().compare(areaId, Qt::CaseInsensitive) == 0) {
            return &it.value();
        }
    }
    return nullptr;
}

// Normalize a path for comparison: replace '/' with '\\' (Windows-friendly).
// The Java reference uses the same normalization (absolutePath.replace('/','\\'))
// because OGG/motion3 paths in voice packs may come in with either separator.
QString normalizePath(const QString& path)
{
    QString out = path;
    out.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return out;
}

} // namespace

// ── hasGroupForArea ──────────────────────────────────────────────────────────

bool MountedBehaviorEngine::hasGroupForArea(const QString& areaId) const
{
    return findGroupCaseInsensitive(m_voicePack, areaId) != nullptr;
}

// ── buildBehaviorCommand ─────────────────────────────────────────────────────

std::optional<BehaviorResult>
MountedBehaviorEngine::buildBehaviorCommand(const QString& areaId) const
{
    if (m_voicePack == nullptr) return std::nullopt;

    const VoicePackGroup* group = findGroupCaseInsensitive(m_voicePack, areaId);
    if (group == nullptr) {
        LOG_DEBUG("MountedBehaviorEngine: no group for area=\"{}\"",
                  areaId.toStdString());
        return std::nullopt;
    }

    // Collect actions that have a non-empty motionPath — only those are
    // eligible for play_motion_ext. Actions with empty motion + non-empty
    // audio belong to buildAudioOnlyCommand.
    QList<VoicePackAction> motionActions;
    motionActions.reserve(group->actions.size());
    for (const VoicePackAction& action : group->actions) {
        if (!action.motionPath.isEmpty()) {
            motionActions.append(action);
        }
    }
    if (motionActions.isEmpty()) {
        LOG_DEBUG("MountedBehaviorEngine: no motion actions for area=\"{}\"",
                  areaId.toStdString());
        return std::nullopt;
    }

    const VoicePackAction& chosen = motionActions.at(
        QRandomGenerator::global()->bounded(motionActions.size()));

    const QDir baseDir(m_voicePack->basePath);
    const QString motionAbs = baseDir.absoluteFilePath(chosen.motionPath);
    const QString audioAbs = chosen.audioPath.isEmpty()
        ? QString() : baseDir.absoluteFilePath(chosen.audioPath);
    const QString lipSyncAbs = chosen.lipSyncPath.isEmpty()
        ? QString() : baseDir.absoluteFilePath(chosen.lipSyncPath);

    const double fadeInSec  = chosen.fadeInMs  / 1000.0;
    const double fadeOutSec = chosen.fadeOutMs / 1000.0;

    LOG_INFO("MountedBehaviorEngine: area=\"{}\" motion=\"{}\" audio=\"{}\" "
             "lipSync=\"{}\"",
             areaId.toStdString(),
             chosen.motionPath.toStdString(),
             chosen.audioPath.toStdString(),
             chosen.lipSyncPath.toStdString());

    const Envelope command = Protocol::buildPlayMotionExt(
        motionAbs,
        group->priority,
        fadeInSec,
        fadeOutSec,
        audioAbs,
        lipSyncAbs);

    return BehaviorResult{command, chosen.doc};
}

// ── buildAudioOnlyCommand ────────────────────────────────────────────────────

std::optional<Envelope>
MountedBehaviorEngine::buildAudioOnlyCommand(const QString& areaId) const
{
    if (m_voicePack == nullptr) return std::nullopt;

    const VoicePackGroup* group = findGroupCaseInsensitive(m_voicePack, areaId);
    if (group == nullptr) {
        LOG_DEBUG("MountedBehaviorEngine: no group for area=\"{}\" (audio-only)",
                  areaId.toStdString());
        return std::nullopt;
    }

    // Eligible actions: audio present AND motion absent.
    QList<VoicePackAction> audioOnlyActions;
    audioOnlyActions.reserve(group->actions.size());
    for (const VoicePackAction& action : group->actions) {
        if (!action.audioPath.isEmpty() && action.motionPath.isEmpty()) {
            audioOnlyActions.append(action);
        }
    }
    if (audioOnlyActions.isEmpty()) {
        LOG_DEBUG("MountedBehaviorEngine: no audio-only actions for area=\"{}\"",
                  areaId.toStdString());
        return std::nullopt;
    }

    const VoicePackAction& chosen = audioOnlyActions.at(
        QRandomGenerator::global()->bounded(audioOnlyActions.size()));

    const QDir baseDir(m_voicePack->basePath);
    const QString audioAbs = baseDir.absoluteFilePath(chosen.audioPath);

    return Protocol::buildPlayAudio(audioAbs, 1.0);
}

// ── isIdleMotionPath ─────────────────────────────────────────────────────────

bool MountedBehaviorEngine::isIdleMotionPath(const QString& absolutePath) const
{
    if (m_voicePack == nullptr) return false;

    const VoicePackGroup* group = findGroupCaseInsensitive(
        m_voicePack, QStringLiteral("idle"));
    if (group == nullptr) return false;

    const QDir baseDir(m_voicePack->basePath);
    const QString normalizedInput = normalizePath(absolutePath);

    for (const VoicePackAction& action : group->actions) {
        if (action.motionPath.isEmpty()) continue;
        const QString resolved = baseDir.absoluteFilePath(action.motionPath);
        if (normalizedInput == normalizePath(resolved)) {
            return true;
        }
    }
    return false;
}

// ── groupNames ───────────────────────────────────────────────────────────────

QStringList MountedBehaviorEngine::groupNames() const
{
    if (m_voicePack == nullptr) return {};
    return m_voicePack->groups.keys();
}

// ── estimateOggDurationMs ────────────────────────────────────────────────────

qint64 MountedBehaviorEngine::estimateOggDurationMs(const QString& oggPath)
{
    QFile file(oggPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARN("MountedBehaviorEngine: OGG open failed \"{}\" → fallback {}ms",
                 oggPath.toStdString(), kFallbackDurationMs);
        return kFallbackDurationMs;
    }
    const qint64 fileSize = file.size();
    if (fileSize < 85) {
        LOG_WARN("MountedBehaviorEngine: OGG too small ({}) \"{}\" → fallback",
                 fileSize, oggPath.toStdString());
        return kFallbackDurationMs;
    }

    // ── 1. Read the header (first 85 bytes) ─────────────────────────────────
    // Find the \x01vorbis codec-id header, extract the sample_rate (uint32 LE
    // at offset +12 from the \x01vorbis marker).
    const qint64 headerLen = std::min<qint64>(85, fileSize);
    const QByteArray header = file.read(headerLen);
    if (header.size() < headerLen) {
        return kFallbackDurationMs;
    }

    const char kVorbisMarker[7] = {0x01, 'v', 'o', 'r', 'b', 'i', 's'};
    int vorbisOff = -1;
    for (int i = 0; i + 7 <= header.size(); ++i) {
        bool match = true;
        for (int j = 0; j < 7; ++j) {
            if (header.at(i + j) != kVorbisMarker[j]) { match = false; break; }
        }
        if (match) { vorbisOff = i; break; }
    }
    if (vorbisOff < 0 || vorbisOff + 16 > header.size()) {
        LOG_WARN("MountedBehaviorEngine: no \\x01vorbis header in \"{}\" → fallback",
                 oggPath.toStdString());
        return kFallbackDurationMs;
    }

    const auto sampleRate = static_cast<quint32>(
        (static_cast<quint8>(header.at(vorbisOff + 12)))
        | (static_cast<quint8>(header.at(vorbisOff + 13)) << 8)
        | (static_cast<quint8>(header.at(vorbisOff + 14)) << 16)
        | (static_cast<quint8>(header.at(vorbisOff + 15)) << 24));
    if (sampleRate == 0) {
        LOG_WARN("MountedBehaviorEngine: vorbis sample_rate=0 in \"{}\" → fallback",
                 oggPath.toStdString());
        return kFallbackDurationMs;
    }

    // ── 2. Scan the last 64KB for the final OggS page ───────────────────────
    // Walk the tail from the END backwards; the first 'OggS' we find is the
    // last page in the file. Its granule_position (int64 LE at +6) is the
    // total sample count at the end of playback → duration =
    // granule_position / sample_rate * 1000 ms.
    const qint64 tailSize = std::min<qint64>(65536, fileSize);
    if (!file.seek(fileSize - tailSize)) {
        return kFallbackDurationMs;
    }
    const QByteArray tail = file.read(tailSize);
    if (tail.size() < tailSize) {
        return kFallbackDurationMs;
    }

    // Need at least 27 bytes from the 'O' to read the granule_position (offset
    // 6, length 8 → bytes 6..13 inclusive; we read 14 bytes total from 'O').
    // Walk from end backwards so we find the LAST page first.
    for (int i = tail.size() - 27; i >= 0; --i) {
        if (tail.at(i) == 'O' && i + 14 <= tail.size()
            && tail.at(i + 1) == 'g' && tail.at(i + 2) == 'g'
            && tail.at(i + 3) == 'S') {
            // Read int64 LE granule_position.
            quint64 granule = 0;
            for (int j = 0; j < 8; ++j) {
                granule |= (static_cast<quint64>(static_cast<quint8>(
                    tail.at(i + 6 + j))) << (8 * j));
            }
            if (granule > 0) {
                const qint64 durationMs = static_cast<qint64>(
                    (granule * 1000ULL) / sampleRate);
                if (durationMs > 0) return durationMs;
            }
            // granule 0 on the last page → unknown; keep scanning for an
            // earlier page with a non-zero granule (continuation pages exist).
        }
    }

    LOG_WARN("MountedBehaviorEngine: no OggS page with granule>0 in \"{}\" → fallback",
             oggPath.toStdString());
    return kFallbackDurationMs;
}

} // namespace core
