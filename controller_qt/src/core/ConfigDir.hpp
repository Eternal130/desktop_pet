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
//   - DynamicPluginSource + PluginManager (pluginsDir() drop-in scan target,
//     stage-2 host channel 2026-10-16)
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

// Returns the user drop-in PLUGIN directory: <dataDir>/plugins/ (stage-2
// dynamic discovery, 2026-10-16; the lower-priority half of
// DynamicPluginSource's scan — <exeDir>/plugins/ wins on same-name
// collisions). Created on demand by its owners (PluginManager::
// openPluginsDir), NOT by ensureDirectories().
//
// NOT to be confused with IPluginContext::pluginConfigDir() — that is the
// per-plugin PRIVATE config area <configDir>/plugins/<id>/ (a different
// root and a different purpose despite the shared "plugins" segment).
QString pluginsDir();

// ── Plugin DATA domains (v1.13 resource isolation, rev2 §2) ───────────────
//
// THREE similarly-named locations, deliberately distinct:
//   - pluginsDir()                 <dataDir>/plugins/       — plugin
//     BINARIES (the v1.12 dynamic-plugin drop-in scan target; lowercase).
//   - pluginConfigDir() [SDK-side] <configDir>/plugins/<id>/ — per-plugin
//     PRIVATE config (IPluginContext). Different root, different purpose.
//   - pluginDataRoot() [here]      <dataDir>/PluginData/    — per-plugin
//     DATA domains: <root>/<pluginId>/Models/<Name>/<Name>.model3.json and
//     <root>/<pluginId>/VoicePacks/<Pack>/meta.mko. Mixed case on purpose:
//     on Windows (case-insensitive FS) "PluginData" cannot collide with the
//     lowercase binary dir "plugins" only by case — the different stem
//     entirely removes the collision class (maintainer decision 2026-10-16;
//     spec rev2 §2's literal "Plugins/" was rejected for exactly that).
//
// Scan-root discipline (rev2 §3.2c): every plugin-domain SCAN enumerates
// pluginDataOwners() — the immediate subdirectories — and NEVER consults the
// loaded plugin registry. Auto-start instance restore (main.cpp) runs
// BEFORE PluginHost::initializeAll(), so disabled/unloaded plugins' domains
// must stay scannable/resolvable by directory traversal alone.
//
// None of these are auto-created by ensureDirectories(); owners (e.g.
// DownloadService's owner-scoped installArchive) mkpath on demand.

// Shared plugin-id gate (v1.13 hardening, oracle follow-up Should#1): the
// §B.6 manifest id charset and the PluginData domain-owner charset are the
// SAME set, so ONE validator serves both PluginRegistry (manifest parse)
// and the domain accessors below. Contract: non-empty, chars in
// [A-Za-z0-9.-] ONLY, and not exactly "." / ".." (the canonical traversal
// names — a dot-only id satisfies the charset but can never name a real
// plugin or domain directory). Anything else — spaces, '/', '\', unicode,
// "a.b\..\.." — is rejected; this closes the Windows backslash-traversal
// hole where a hostile owner id could escape <pluginDataRoot>/<id>/.
// NOTE: the manifest-level "must contain a dot" reverse-domain SHAPE rule
// stays in PluginRegistry — a validator pass does not by itself make an
// id a legal manifest id.
bool isValidPluginDataOwnerId(const QString& id);

// Returns the plugin DATA-domain root: <dataDir>/PluginData/ (trailing
// '/'). NOT auto-created — scanners must tolerate a missing root (they
// simply see no plugin domains).
QString pluginDataRoot();

// Returns <pluginDataRoot>/<pluginId>/Models/ (joined, trailing '/',
// NOT created). An EMPTY or charset-invalid pluginId (see
// isValidPluginDataOwnerId) returns an empty string — an empty scope
// means the HOST domain, and a traversal-bearing id must never be
// concatenated into a path.
QString pluginModelsDir(const QString& pluginId);

// Returns <pluginDataRoot>/<pluginId>/VoicePacks/ (joined, trailing '/',
// NOT created). Same invalid-pluginId contract as pluginModelsDir().
QString pluginVoicePacksDir(const QString& pluginId);

// Immediate subdirectories of pluginDataRoot(), sorted
// case-insensitively, hidden dirs ('.'-prefixed) skipped, and — v1.13
// hardening — entries outside the documented id charset SKIPPED with a
// WARN (a hostile directory name can never become an owner id this way).
// A missing root yields an empty list (never an error). These are the
// plugin-domain OWNER ids for scans — derived purely from directory
// traversal (§3.2c).
//
// For testing: accepts an injectable root (empty = the real
// pluginDataRoot(); same seam discipline as ensureDirectories' basePath)
// so the hostile-dir filter is testable without touching the user's
// real data dir.
QStringList pluginDataOwners(const QString& root = {});

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
