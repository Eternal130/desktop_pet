#pragma once

#include <QString>
#include <QStringList>

// VoicePackScanner (Phase 5 Wave 8 todo 19) — pure static file-scanning
// function that enumerates installed voice packs. This is the Qt/C++ port of
// the Java reference controller/.../core/VoicePackScanner.scanAvailableVoicePacks
// (44 LOC), extended with a second user-packs source (storage-layout
// revision 2026-09).
//
// Two sources:
//   1. BUILT-IN: the renderer's bundled
//      <rendererDir>/Resources/VoicePacks/<PackName>/meta.mko (ships with
//      the app).
//   2. USER: <userPacksDir>/<PackName>/meta.mko — userPacksDir IS the
//      VoicePacks directory itself (e.g. ConfigDir::userVoicePacksDir() =
//      <dataDir>/VoicePacks/), NOT a "Resources" parent. Packs downloaded
//      at runtime install here (DownloadService installRoot).
//
// On a name collision (same directory basename in both sources) the USER
// pack wins — it replaces the built-in entry in the result.
//
// Consumed by:
//   - the mount UI (VoicePackController): populate the voice-pack ComboBox
//   - the plugin VoicePackApi (PluginCapabilityStubs::listPacks)
//
// Contract: NEVER throws. A missing directory, a non-directory root, or any
// I/O failure → an empty QStringList. The caller renders "no voice packs
// found" rather than crashing. This matches the ModelScanner contract
// (Phase 5 todo 4).

namespace core {

// Enumerate available voice-pack directories. See the header block comment
// for the two sources + collision rule.
//
// A subdirectory qualifies when it contains a file named "meta.mko" (the
// voice-pack index file — a protobuf Bundle, parsed by MetaMkoParser).
// Qualified names are returned sorted case-insensitively.
//
// userPacksDir may be empty (built-in scan only). Returns an empty list
// when neither source yields a qualifying pack. Never throws — QDir
// operations are exception-free in Qt.
QStringList scanAvailableVoicePacks(const QString& rendererDir,
                                    const QString& userPacksDir);

// Backward-compatible single-source overload: built-in scan only (equivalent
// to passing an empty userPacksDir above).
QStringList scanAvailableVoicePacks(const QString& rendererDir);

} // namespace core
