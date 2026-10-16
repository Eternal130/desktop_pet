#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// VoicePackScanner (Phase 5 Wave 8 todo 19) — pure static file-scanning
// function that enumerates installed voice packs. This is the Qt/C++ port of
// the Java reference controller/.../core/VoicePackScanner.scanAvailableVoicePacks
// (44 LOC), extended with a second user-packs source (storage-layout
// revision 2026-09).
//
// Two HOST sources:
//   1. BUILT-IN: the renderer's bundled
//      <rendererDir>/Resources/VoicePacks/<PackName>/meta.mko (ships with
//      the app).
//   2. USER: <userPacksDir>/<PackName>/meta.mko — userPacksDir IS the
//      VoicePacks directory itself (e.g. ConfigDir::userVoicePacksDir() =
//      <dataDir>/VoicePacks/), NOT a "Resources" parent. Packs downloaded
//      at runtime install here (DownloadService installRoot).
//
// On a HOST name collision (same directory basename in both host sources)
// the USER pack wins — it replaces the built-in entry in the result.
//
// v1.13 resource isolation (rev2 §3.2a): scanVoicePacksScoped adds PLUGIN
// DATA domains — <pluginDataRoot>/<owner>/VoicePacks/<Pack>/meta.mko.
// Plugin entries are keyed per (owner, name): the SAME pack name in two
// plugin domains yields BOTH entries, and a plugin pack NEVER shadows a
// host pack (or vice versa) — the host dual-source user-wins rule applies
// WITHIN the host domain only.
//
// Consumed by:
//   - the mount UI (VoicePackController): populate the voice-pack ComboBox
//     (host-only projection — scanAvailableVoicePacks; the controller
//     filters plugin-domain entries out of the page list, rev2 §3.2b)
//   - the plugin VoicePackApi (PluginCapabilityStubs::listPacks)
//
// Contract: NEVER throws. A missing directory, a non-directory root, or any
// I/O failure → an empty contribution from that source. The caller renders
// "no voice packs found" rather than crashing. This matches the
// ModelScanner contract (Phase 5 todo 4).

namespace core {

// v1.13: one discovered voice pack with its resource-domain ownership.
//   dirPath  — ABSOLUTE pack directory (contains meta.mko) — the value the
//              2-source scan has always returned.
//   owner    — "" = HOST domain (built-in + user merge); a non-empty
//              plugin id = the plugin-data domain
//              (<pluginDataRoot>/<owner>/VoicePacks/).
struct ScannedPack {
    QString dirPath;
    QString owner;
};

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

// v1.13 (rev2 §3.2a): enumerate voice packs across the HOST domain plus
// every plugin data domain under pluginDataRoot.
//
// The host half keeps the dual-source user-wins merge WITHIN the host
// domain (owner=""). Each immediate subdirectory
// <pluginDataRoot>/<owner>/ contributes its VoicePacks/<Pack>/meta.mko
// entries with owner = the directory name. Entries are keyed per
// (owner, name): the same pack name in two plugin domains appears TWICE
// (distinct dirPaths), and plugin entries never merge with or shadow host
// entries.
//
// pluginDataRoot may be empty (host-only). A missing root contributes no
// plugin entries. Result sorted by (owner, dirPath) case-insensitively —
// host entries first.
//
// Scan-root discipline (rev2 §3.2c): the plugin-domain walk is PURE
// directory traversal — the loaded plugin registry is never consulted.
QVector<ScannedPack> scanVoicePacksScoped(const QString& rendererDir,
                                          const QString& userPacksDir,
                                          const QString& pluginDataRoot);

} // namespace core
