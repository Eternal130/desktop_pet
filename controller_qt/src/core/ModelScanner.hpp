#pragma once

#include <QString>
#include <QStringList>

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
// Consumed by:
//   - todo 6 (InstanceDetailPage UI): populate the model-selection ComboBox.
//
// Contract: NEVER throws. A missing <rendererDir>/Resources/Models/ tree, a
// non-directory rendererDir, or any I/O failure → an empty QStringList. The
// caller renders "no models found" rather than crashing.

namespace core {

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

} // namespace core
