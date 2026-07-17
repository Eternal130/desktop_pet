#pragma once

#include <QString>
#include <QStringList>

// VoicePackScanner (Phase 5 Wave 8 todo 19) — pure static file-scanning
// function that enumerates installed voice packs under a renderer's bundled
// Resources/ tree. This is the Qt/C++ port of the Java reference
// controller/.../core/VoicePackScanner.scanAvailableVoicePacks (44 LOC).
//
// The renderer ships voice packs at
//   <rendererDir>/Resources/VoicePacks/<PackName>/meta.mko
// where each <PackName> directory is one voice pack. scanAvailableVoicePacks
// lists every such directory name, sorted case-insensitively.
//
// Consumed by:
//   - todo 21 (mount UI): populate the voice-pack selection ComboBox.
//
// Contract: NEVER throws. A missing <rendererDir>/Resources/VoicePacks/ tree,
// a non-directory rendererDir, or any I/O failure → an empty QStringList.
// The caller renders "no voice packs found" rather than crashing. This
// matches the ModelScanner contract (Phase 5 todo 4).

namespace core {

// Enumerate available voice-pack directories under
// <rendererDir>/Resources/VoicePacks/.
//
// A subdirectory qualifies when it contains a file named "meta.mko" (the
// voice-pack index file — a protobuf Bundle, parsed by MetaMkoParser).
// Qualified names are returned sorted case-insensitively.
//
// Returns an empty list when:
//   - rendererDir is empty
//   - <rendererDir>/Resources/VoicePacks/ does not exist or is not a directory
//   - no subdirectory contains meta.mko
//
// Never throws — QDir operations are exception-free in Qt.
QStringList scanAvailableVoicePacks(const QString& rendererDir);

} // namespace core
