#pragma once

#include <QString>

// Cross-platform config directory management (task T17).
//
// All config lives under the user's home directory in a single XDG-style
// location that is IDENTICAL on both platforms:
//
//   Linux:   ~/.config/desktop-pet/
//   Windows: %USERPROFILE%\.config\desktop-pet\
//
// This intentionally does NOT follow the platform convention (%APPDATA% on
// Windows / QStandardPaths::AppDataLocation) — the path must stay
// byte-for-byte compatible with the legacy config format rooted under the
// user's home directory. QDir::homePath() resolves USERPROFILE on Windows
// (NOT the HOME env var), which pins the same location on every supported
// OS. See architecture-blueprint.md §4.5.1 for the cross-architecture
// rationale.
//
// Consumed by:
//   - T18-T22 config managers (each takes an injectable base path for testing)
//   - T2 Logging (logsDir() provides the rotating-file-sink root)
//
// Windows caveat (m6): if a user launches the app under MSYS2 / Git Bash with a
// HOME env var set, that env var is NOT consulted by QDir::homePath() (Qt reads
// USERPROFILE first on Windows). The path may then differ from a shell that
// honors HOME. See controller_qt/README.md §"Config directory (HOME caveat)".

namespace ConfigDir {

// Returns the config directory path: <home>/.config/desktop-pet/
// QDir::homePath() resolves USERPROFILE on Windows (matching Java's user.home),
// NOT the HOME env var. The result ALWAYS uses '/' separators, even on Windows.
QString configDir();

// Returns the instances subdirectory: <configDir>/instances/
QString instancesDir();

// Returns the logs subdirectory: <configDir>/logs/
QString logsDir();

// Creates the config directory tree (configDir + instances/ + logs/) if it does
// not already exist. Returns true on success.
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
