#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <optional>

// ModelInfoParser (Phase 5 todo 4) — pure static file-parsing functions that
// extract the model metadata the controller needs but the renderer's
// `model_loaded` event does NOT carry (interface.md §B: motions/expressions
// arrive as EMPTY arrays). The controller therefore parses the .model3.json
// directly. This is the Qt/C++ port of the Java reference
// controller/.../core/ModelInfoParser.java (109 LOC).
//
// Consumed by:
//   - todo 5 (InstanceSession lifecycle): on `model_loaded`, call parse() on
//     the instance's <modelName>.model3.json → send set_hit_areas with the
//     parsed hitAreas (the renderer cannot report them).
//   - todo 6 (InstanceDetailPage UI): display motionGroups + expressions.
//
// Contract: NEVER throws. Missing file, unreadable file, or unparseable JSON
// → std::nullopt. A root object that happens to omit FileReferences / Motions
// / Expressions / HitAreas → a populated ModelInfo with the missing piece left
// empty (matches the Java reference's per-field graceful degradation — only
// the root-level parse failure returns std::nullopt).

namespace core {

// Parsed model metadata extracted from a .model3.json file. Plain struct
// (mirrors InstanceConfig's public-fields style — no getters/setters).
//
//   motionGroups: group name → motion count. Each key under
//                 FileReferences.Motions maps to an array; the value is that
//                 array's length (e.g. Hiyori: {"Idle": 9, "TapBody": 1}).
//   expressions:  expression names from FileReferences.Expressions[].Name,
//                 in document order. Empty when the model defines none.
//   hitAreas:     hit-area names from HitAreas[].Name, in document order.
//                 Used to populate the set_hit_areas command payload.
struct ModelInfo {
    QMap<QString, int> motionGroups;
    QStringList expressions;
    QStringList hitAreas;
};

// Parse a .model3.json file at `model3JsonPath`.
//
// Returns std::nullopt when:
//   - the file does not exist or cannot be opened
//   - the bytes are not valid JSON
//   - the JSON root is not an object
//
// Otherwise returns a ModelInfo. Missing FileReferences / Motions /
// Expressions / HitAreas produce empty containers within the returned
// ModelInfo (NOT std::nullopt) — the renderer's set_hit_areas command is
// legal with an empty array, and the UI gracefully shows "no motions".
//
// Never throws. All structural navigation uses QJsonObject::contains() +
// isObject() / isArray() guards; malformed sub-trees are skipped silently.
std::optional<ModelInfo> parseModelInfo(const QString& model3JsonPath);

} // namespace core
