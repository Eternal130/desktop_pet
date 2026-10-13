#pragma once

#include <QString>
#include <QStringList>

#include <optional>

#include "core/VoicePackInfo.hpp"
#include "network/Envelope.hpp"

// MountedBehaviorEngine (Phase 5 Wave 8 todo 20) — when a voice
// pack is mounted, this engine has PRIORITY over InteractionHandler for hit
// events: if the voice pack has a group for the hit area, the engine builds a
// play_motion_ext command (motion + audio + lipSync side-channels); otherwise
// the hit falls through to InteractionHandler's play_motion path
// (InstanceSession::handleHitEvent routes between the two).
//
// Construction:
//   - MountedBehaviorEngine(nullptr) → "engine disabled" sentinel.
//     hasGroupForArea() always returns false; buildBehaviorCommand() returns
//     std::nullopt. The hit handler falls back to InteractionHandler.
//   - MountedBehaviorEngine(&pack)   → engine enabled for the given voice
//     pack. The voice pack's `basePath` is used to resolve the relative
//     motion/audio/lipSync paths in each VoicePackAction to absolute paths.
//
// API (mirrors the Java reference, adapted to C++):
//   - hasGroupForArea(areaId)        — case-insensitive group lookup against
//                                      voicePack.groups. False if no voice pack.
//   - buildBehaviorCommand(areaId)   — pick a random action with non-empty
//                                      motionPath from groups[areaId], resolve
//                                      motion/audio/lipSync against basePath,
//                                      and build a play_motion_ext Envelope via
//                                      Protocol::buildPlayMotionExt.
//   - buildAudioOnlyCommand(areaId)  — like the above but for actions with NO
//                                      motionPath (audio-only): builds a
//                                      play_audio command instead.
//   - isIdleMotionPath(absPath)      — true if absPath matches one of the
//                                      voice pack's "idle" group's motion
//                                      paths (used by the motion_finished
//                                      handler to judge idle vs non-idle).
//   - hasIdleMotions()               — the pack can supply idle motions
//                                      (mounted + "idle" group + at least one
//                                      motion-bearing action).
//   - idleMotionCount()              — size of the pack's motion-bearing
//                                      idle pool.
//   - buildIdleMotionCommand(index)  — play_motion_ext for the index-th
//                                      motion of the pack's "idle" group at
//                                      FIXED priority=1 (Idle tier).
//   - groupNames()                   — the voice pack's group codes (empty if
//                                      no voice pack).
//
// estimateOggDurationMs:
//   A ~50 LOC heuristic that parses the OGG container directly (no libvorbis
//   dependency): find the \x01vorbis codec-id header → extract the sample rate
//   (uint32 LE at +12); scan the last 64KB for the final OggS page → read its
//   granule_position (int64 LE at +6); duration = granule / sample_rate * 1000.
//   On ANY failure → return kFallbackDurationMs (5000). NEVER throws.
//   NOTE: libvorbis is the alternative (precise decode-based duration); the
//   heuristic is preferred here to keep deps minimal — see the file-level
//   comment in MountedBehaviorEngine.cpp.
//
// Standalone for todo 20 (tested independently via MountedBehaviorEngineTest).
// InstanceSession::handleHitEvent consults the engine first (when present) and
// routes to play_motion_ext; otherwise falls back to InteractionHandler. When
// the chosen action carries dialogue text (the .mko doc field), the caller
// forwards it to the notification bubble stream — it is never sent to the
// renderer as a subtitle.
//
// Threading: not thread-safe. Used only on the Qt main thread (the WS callback
// thread that delivers hit events). No internal mutable state besides the
// QRandomGenerator used for action selection.

namespace core {

// Result of a successful buildBehaviorCommand call. Carries the built
// play_motion_ext Envelope + the action's dialogue text for the caller to
// forward to the notification bubble stream (bubble duration comes from the
// stream's own default, not from the audio).
//
// `command.action == "play_motion_ext"`; `dialogueText` is action.doc and may
// be empty when the action had no doc field.
struct BehaviorResult {
    Envelope command;
    QString dialogueText;
};

class MountedBehaviorEngine {
public:
    // Fallback duration returned by estimateOggDurationMs on any failure
    // (missing file, not-an-OGG, no vorbis header, no OggS page). 5000ms
    // matches the Java reference and is a reasonable default for a subtitle
    // that has no audio to time against.
    static constexpr qint64 kFallbackDurationMs = 5000;

    // Construct an engine bound to a voice pack. `voicePack` is borrowed
    // (NOT owned) — the caller MUST guarantee it outlives this engine. Pass
    // nullptr (or default-construct) for an "engine disabled" sentinel
    // (hasGroupForArea always returns false, buildBehaviorCommand returns
    // std::nullopt). The default-constructed form is what InstanceSession
    // holds by default; todo 21 swaps in a populated VoicePackInfo when the
    // user mounts a voice pack.
    explicit MountedBehaviorEngine(const VoicePackInfo* voicePack = nullptr) noexcept
        : m_voicePack(voicePack) {}

    // Swap the borrowed voice pack pointer. nullptr disables the engine.
    // Used by todo 21 when the user mounts/unmounts a voice pack. The caller
    // owns the VoicePackInfo and must keep it alive for the engine's lifetime.
    void setVoicePack(const VoicePackInfo* voicePack) noexcept {
        m_voicePack = voicePack;
    }

    // True if the voice pack has a group for `areaId` (case-insensitive,
    // matching InteractionHandler's case-folding convention). False when no
    // voice pack is mounted or the area is not in the group map.
    bool hasGroupForArea(const QString& areaId) const;

    // Build a play_motion_ext command for `areaId`. Returns std::nullopt when:
    //   - no voice pack is mounted; OR
    //   - groups[areaId] is absent; OR
    //   - the group has no actions with a non-empty motionPath.
    //
    // On success: picks a RANDOM action (QRandomGenerator), resolves the
    // action's motion/audio/lipSync paths to ABSOLUTE paths against
    // voicePack.basePath, and builds the play_motion_ext Envelope via
    // Protocol::buildPlayMotionExt. fadeIn/fadeOut are converted from the
    // action's ms fields to seconds. The action's doc text is returned as
    // BehaviorResult::dialogueText.
    std::optional<BehaviorResult> buildBehaviorCommand(const QString& areaId) const;

    // Build a play_audio command for `areaId`. Picks from actions that have
    // audio but NO motion (audio-only actions). Returns std::nullopt when no
    // voice pack / no group / no audio-only actions. The volume is fixed at
    // 1.0 (mirrors the Java reference).
    std::optional<Envelope> buildAudioOnlyCommand(const QString& areaId) const;

    // True if `absolutePath` matches one of the voice pack's "idle" group
    // action motion paths (resolved to absolute against basePath). Used by the
    // motion_finished handler to judge whether a voice-pack motion was idle or
    // non-idle (so triggerNow can resume the idle cycle correctly).
    // Path comparison uses '/'→'\\' normalization (Windows-friendly, matches
    // the Java reference). False when no voice pack or no "idle" group.
    bool isIdleMotionPath(const QString& absolutePath) const;

    // True when the mounted pack can supply idle motions: a voice pack is
    // mounted AND it has an "idle" group (case-insensitive, the same
    // convention as hasGroupForArea) AND that group holds at least one
    // action with a non-empty motionPath. When true, startIdleScheduler
    // prefers the pack's idle motions over the model's Idle group (pack >
    // model > none — a motionless model still idles via its pack).
    bool hasIdleMotions() const;

    // Number of "idle"-group actions with a non-empty motionPath — the pool
    // buildIdleMotionCommand indexes into. 0 when no voice pack / no "idle"
    // group / no motion-bearing actions in the group.
    int idleMotionCount() const;

    // Build a play_motion_ext Envelope for the `index`-th motion-bearing
    // action of the "idle" group (0-based over the same filtered pool as
    // idleMotionCount). Returns std::nullopt when no voice pack / no "idle"
    // group / `index` out of range. priority is FIXED at 1 (the Idle tier
    // per interface.md §B.1 — behavior motions play at 2+ and may interrupt
    // an idle motion). fadeIn/fadeOut convert from the action's ms fields to
    // seconds (the buildBehaviorCommand math). audioPath/lipSyncPath ride
    // along when the action carries them (typical pack idles have neither —
    // no special-casing, the empty-string passthrough covers it).
    std::optional<Envelope> buildIdleMotionCommand(int index) const;

    // The voice pack's group codes (keys of voicePack.groups). Empty when no
    // voice pack is mounted. Used by diagnostic UI / logging.
    QStringList groupNames() const;

    // OGG duration heuristic (exposed public for testing). Reads the file at
    // `oggPath`, parses the vorbis identification header + final OggS page,
    // and returns the duration in milliseconds. Returns
    // kFallbackDurationMs on ANY failure (missing file, not an OGG, no vorbis
    // header, no OggS page, sample rate <= 0). NEVER throws.
    static qint64 estimateOggDurationMs(const QString& oggPath);

private:
    const VoicePackInfo* m_voicePack; // borrowed, may be nullptr
};

} // namespace core
