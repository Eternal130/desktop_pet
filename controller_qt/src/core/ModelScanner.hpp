#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// ModelScanner (Phase 5 todo 4) — pure static file-scanning function that
// enumerates the available Live2D models under a renderer's bundled
// Resources/ tree. This is the Qt/C++ port of the Java reference
// controller/.../core/ModelScanner.scanAvailableModels (87 LOC).
//
// The renderer ships its models at <rendererDir>/Resources/Models/<Name>/
// where each <Name> directory contains <Name>.model3.json (the Cubism
// Samples convention — e.g. Hiyori/Hiyori.model3.json). scanAvailableModels
// lists every such directory name, sorted case-insensitively.
//
// v1.13 resource isolation (rev2 §3): scanModelsScoped extends the scan
// with PLUGIN DATA domains — <pluginDataRoot>/<owner>/Models/<Name>/ with
// the same <Name>/<Name>.model3.json qualifier. Host entries carry
// owner="" (the renderer's bundled tree); plugin entries carry owner =
// the immediate directory name under pluginDataRoot. There is NO
// cross-domain deduplication: a host "Hiyori" and a plugin-domain
// "Hiyori" are DISTINCT resources that both appear in the result.
//
// Consumed by:
//   - todo 6 (InstanceDetailPage UI): populate the model-selection ComboBox
//     (host-only projection — scanAvailableModels).
//   - v1.13 wave-2 pet.model scans (scoped form).
//
// Contract: NEVER throws. A missing root, a non-directory rendererDir, or
// any I/O failure → those sources simply contribute no entries. The caller
// renders "no models found" rather than crashing.

namespace core {

// v1.13: one discovered model with its resource-domain ownership.
//   name           — bare directory name (the logical model name; the
//                    load_model wire `model_path`).
//   owner          — "" = HOST domain (renderer Resources/Models); a
//                    non-empty plugin id = the plugin-data domain the
//                    model lives under (<pluginDataRoot>/<owner>/Models/).
//   modelJsonPath  — ABSOLUTE path of the qualifying <Name>.model3.json.
struct ScannedModel {
    QString name;
    QString owner;
    QString modelJsonPath;
};

// Enumerate available model directories under <rendererDir>/Resources/Models/.
//
// A subdirectory qualifies when it contains a file named
// "<subdir>.model3.json" (the Cubism Samples naming convention). Qualified
// names are returned sorted case-insensitively (Qt's default sort is case
// insensitive; we make it explicit so the UI is stable across platforms).
//
// Returns an empty list when:
//   - rendererDir is empty
//   - <rendererDir>/Resources/Models/ does not exist or is not a directory
//   - no subdirectory contains the expected .model3.json file
//
// Never throws — QDir/QFile operations are exception-free in Qt.
QStringList scanAvailableModels(const QString& rendererDir);

// v1.13 (rev2 §3): enumerate models across the HOST domain plus every
// plugin data domain under pluginDataRoot.
//
// Host entries are byte-identical to scanAvailableModels' discovery
// (owner="", absolute modelJsonPath under <rendererDir>/Resources/Models).
// Each immediate subdirectory <pluginDataRoot>/<owner>/ is scanned for
// Models/<Name>/<Name>.model3.json with the same qualifier; owner is the
// directory name. NO cross-domain dedup — same-name models in different
// domains coexist as distinct entries.
//
// pluginDataRoot may be empty (host-only scan — identical result shape to
// scanAvailableModels plus the path/owner columns). A missing root simply
// contributes no plugin entries. Result sorted by (owner, name)
// case-insensitively — host entries (owner="") first.
//
// Scan-root discipline (rev2 §3.2c): the plugin-domain walk is PURE
// directory traversal — the loaded plugin registry is never consulted.
QVector<ScannedModel> scanModelsScoped(const QString& rendererDir,
                                       const QString& pluginDataRoot);

// v1.13 (rev2 §3.2c): strict plugin-domain resolution — the
// InstanceSession model-path seam's built-in fallback. Resolves
// <pluginDataRoot>/<pluginId>/Models/<modelName>/<modelName>.model3.json
// and returns that ABSOLUTE path when it exists, or QString() on any miss
// (missing root/domain/model, empty pluginId or modelName). NEVER falls
// back to the host domain — a miss is a miss (strict domain resolution).
QString resolvePluginModelPath(const QString& pluginDataRoot,
                               const QString& pluginId,
                               const QString& modelName);

} // namespace core
