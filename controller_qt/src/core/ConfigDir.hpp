#pragma once

#include <QString>

// Cross-platform config/data directory management (task T17; storage-layout
// revision 2026-09: Windows is a per-user app — installer to
// %LOCALAPPDATA%\DesktopPet, no UAC — so storage follows the platform
// conventions via QStandardPaths):
//
//   Linux:   config  ~/.config/desktop-pet/         (UNCHANGED, byte-identical)
//            data    ~/.local/share/desktop-pet/
//   Windows: config  %APPDATA%\desktop-pet\         (Roaming)
//            data    %LOCALAPPDATA%\desktop-pet\    (Local)
//
// All locations are anchored on QStandardPaths::writableLocation() with the
// application name "desktop-pet" and a deliberately EMPTY organization name
// (both set early in main.cpp) — an empty org name keeps every location a
// flat <root>/desktop-pet with no extra org-name path segment. On Linux
// AppConfigLocation still resolves to ~/.config/desktop-pet/, so the
// pre-existing Linux layout is byte-for-byte unchanged.
//
// Consumed by:
//   - config managers (each takes an injectable base path for testing)
//   - T2 Logging (logsDir() provides the rotating-file-sink root)
//   - DownloadService (downloadsDir() staging / userVoicePacksDir() installs)
//
// Migration (Windows only): the legacy layout rooted everything at
// %USERPROFILE%\.config\desktop-pet\. migrateLegacyIfNeeded() copies that
// tree into the new config dir once and renames the legacy dir to
// "<legacy>-migrated-backup". Linux needs NO migration — the path is
// unchanged there.
//
// Linux/XDG note (m6): QStandardPaths honors XDG_CONFIG_HOME /
// XDG_DATA_HOME, so a shell exporting nonstandard XDG vars will see the app
// follow them. (Historical caveat — QDir::homePath() ignoring HOME under
// MSYS2/Git Bash — is obsolete on Windows: the path no longer derives from
// homePath() there.) See controller_qt/README.md §"Config directory".

namespace ConfigDir {

// Returns the config directory path (Linux: ~/.config/desktop-pet/,
// Windows: %APPDATA%/desktop-pet/). The result ALWAYS uses '/' separators
// and a trailing '/', even on Windows.
QString configDir();

// Returns the writable per-user DATA directory (Linux:
// ~/.local/share/desktop-pet/, Windows: %LOCALAPPDATA%/desktop-pet/) with a
// trailing '/'. Root of the user voice packs and download staging.
QString dataDir();

// Returns the user-installed voice-pack directory: <dataDir>/VoicePacks/.
// Created on demand by its owners (DownloadService), NOT by
// ensureDirectories().
QString userVoicePacksDir();

// Returns the download staging directory: <dataDir>/downloads/.
QString downloadsDir();

// Returns the instances subdirectory: <configDir>/instances/
QString instancesDir();

// Returns the logs subdirectory: <configDir>/logs/
QString logsDir();

// One-time legacy-config migration (Windows only — see the header block
// comment; on any other platform this is a compile-time no-op returning
// false, because the Linux path is unchanged).
//
// Behavior (Windows):
//   - legacy dir missing                      → no-op, false
//   - new dir exists AND is non-empty         → no-op, false (fresh install
//     or already migrated — copying legacy state over newer files could
//     clobber them)
//   - otherwise: recursively COPY the legacy tree into the new dir
//     (relative paths preserved; a file that fails to copy logs WARN and
//     the walk continues — a partial migration beats none), then rename the
//     legacy dir to "<legacy>-migrated-backup" (a failed rename only logs
//     WARN — the copy already succeeded). Returns true when the copy ran.
//
// For testing: both paths are injectable. Defaults: legacy =
// <home>/.config/desktop-pet, new = configDir().
//
// Contract: NEVER throws, never blocks beyond the copy itself. Must run
// BEFORE ensureDirectories() — the scaffold (instances/ + logs/) would
// otherwise make a fresh target look already-populated and skip migration.
bool migrateLegacyIfNeeded(const QString& legacyBase = {},
                           const QString& newBase = {});

// Creates the config directory tree (configDir + instances/ + logs/) if it
// does not already exist. Returns true on success. Does NOT create the
// dataDir()/userVoicePacksDir()/downloadsDir() trees — those belong to their
// owners.
//
// For testing: accepts an injectable base path. If basePath is empty, the real
// configDir() is used; otherwise basePath is treated as the config-dir root
// (a trailing separator is added if absent so that "<root>instances" joins
// cleanly regardless of how basePath was supplied). mkpath() is idempotent, so
// a second call on an existing tree is a no-op that still returns true.
//
// A read-only / unwritable location is handled gracefully: returns false and
// logs a WARN (does not throw).
bool ensureDirectories(const QString& basePath = {});

} // namespace ConfigDir
