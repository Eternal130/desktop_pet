#pragma once

#include <QList>
#include <QMap>
#include <QString>

// VoicePackInfo / VoicePackGroup / VoicePackAction / VoicePackModule
// (Phase 5 Wave 8 todo 19) — plain structs forming the parsed view of a voice
// pack's meta.mko file (a protobuf Bundle per src/protobuf/bundles.proto),
// consumed by todo 20 (MountedBehaviorEngine) to map hit events →
// play_motion_ext commands.
//
// Design notes:
//   - Plain structs with public fields (no getters/setters/Q_OBJECT) — mirrors
//     InstanceConfig / PanelConfig / ModelInfo style. The Java side uses
//     records (immutable); C++ structs default to value semantics, which is
//     what we want for a parsed snapshot.
//   - Empty QString replaces Java's null for "absent optional string" (e.g.
//     Action.motion when the protobuf string was empty). Callers check
//     .isEmpty() — identical semantics, C++-idiomatic.
//   - QMap<QString, VoicePackGroup> keyed by group code (ActionGroup.code in
//     bundles.proto). The Java reference uses LinkedHashMap (insertion-ordered);
//     QMap is key-sorted, which is deterministic and testable. The behavior
//     engine (todo 20) resolves by key lookup, so iteration order is cosmetic.
//   - fadeInMs / fadeOutMs are qint64 (bundles.proto declares them int64) —
//     Java's long maps to qint64.

namespace core {

// A single voice-pack action — one motion/audio/lip-sync tuple inside a group.
// Mirrors Java record VoicePackAction(int id, String motionPath, String
// audioPath, String lipSyncPath, String doc, long fadeInMs, long fadeOutMs).
struct VoicePackAction {
    int id = 0;
    QString motionPath;
    QString audioPath;
    QString lipSyncPath;
    QString doc;
    qint64 fadeInMs = 0;
    qint64 fadeOutMs = 0;
};

// A named group of actions (e.g. "tap_head" → all head-tap responses).
// Mirrors Java record VoicePackGroup(String code, String name, int priority,
// List<VoicePackAction> actions).
struct VoicePackGroup {
    QString code;
    QString name;
    int priority = 0;
    QList<VoicePackAction> actions;
};

// A behavior module loaded from the voice pack (key → file mapping with a
// priority for resolution ordering).
// Mirrors Java record VoicePackModule(String key, int priority, String filePath).
struct VoicePackModule {
    QString key;
    int priority = 0;
    QString filePath;
};

// The full parsed voice-pack metadata — the top-level result of
// MetaMkoParser::parse. Carries the directory identity + all groups/modules
// needed by todo 20 (MountedBehaviorEngine).
// Mirrors Java record VoicePackInfo(String dirName, String displayName,
// String code, Path basePath, Map<String, VoicePackGroup> groups,
// List<VoicePackModule> modules).
struct VoicePackInfo {
    QString dirName;
    QString displayName;
    QString code;
    QString basePath;
    QMap<QString, VoicePackGroup> groups;
    QList<VoicePackModule> modules;
};

} // namespace core
