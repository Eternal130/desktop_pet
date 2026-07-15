#pragma once

#include <QObject>
#include <QString>

#include "core/PanelConfig.hpp"

// PanelStateManager (task T21) — load/save the panel-level UI state at
// <configDir>/panel.json (configuration.md §4), with one-shot migration from
// the legacy single-file <configDir>/panel-state.json format that the JavaFX
// controller wrote before the per-instance split.
//
// On load(), three branches are tried in order:
//   1. panel.json exists           → parse via panelConfigFromJson (T19).
//   2. panel-state.json exists     → migrateLegacy(): split the legacy file
//                                    into panel.json + instances/<uuid>.json,
//                                    back up the old file as .bak.
//   3. neither exists (fresh)      → write defaults to panel.json, return them.
//
// Atomicity: save() writes via QSaveFile (the T20 InstanceConfigManager
// pattern) so a crash mid-write cannot corrupt the existing panel.json.
//
// basePath injection: tests pass a QTemporaryDir path so they never touch the
// user's real ~/.config/desktop-pet/. If basePath is empty (the production
// default), the real ConfigDir::configDir() is used. This mirrors the
// InstanceConfigManager (T20) + ConfigDir::ensureDirectories (T17) injection
// pattern.
//
// This is the Qt-side analogue of the Java
// controller/.../core/PanelStateManager.java class. The Qt panel.json uses the
// FLAT snake_case layout written by panelConfigToJson (T19); the legacy
// panel-state.json parsed by migrateLegacy() uses the NESTED `{panel:{...},
// instances:[...]}` layout the Java controller wrote — see migrateLegacy() for
// the exact field mapping.
class PanelStateManager : public QObject
{
    Q_OBJECT

public:
    // basePath: the config-dir root (e.g. ~/.config/desktop-pet/). For tests,
    // inject a QTemporaryDir path. If empty (the default), uses the real
    // ConfigDir::configDir().
    explicit PanelStateManager(const QString& basePath = {},
                               QObject* parent = nullptr);

    // Load panel.json. If it doesn't exist but legacy panel-state.json does,
    // migrate: extract instance state, write panel.json, back up old file.
    // If neither exists, write defaults. Returns the loaded/migrated/default
    // config. NEVER throws — unparseable panel.json or panel-state.json both
    // degrade to defaults with a WARN (legacy file preserved, not renamed).
    PanelConfig load();

    // Save panel.json atomically (QSaveFile pattern from T20). Returns true on
    // success, false on write/rename failure (the existing panel.json is left
    // untouched on failure).
    bool save(const PanelConfig& cfg);

private:
    QString m_basePath;

    // <basePath>/panel.json (or <ConfigDir::configDir()>panel.json when empty).
    QString panelJsonPath() const;

    // <basePath>/panel-state.json — the legacy single-file format.
    QString legacyStatePath() const;

    // <basePath>/panel-state.json.bak — the backup of the legacy file after a
    // successful migration.
    QString legacyBackupPath() const;

    // Migrate legacy panel-state.json → panel.json + instances/*.json. The
    // legacy file is renamed to panel-state.json.bak on success. Returns the
    // migrated PanelConfig. On unparseable legacy input, returns defaults and
    // PRESERVES the old file (no rename) — see implementation for the WARN.
    PanelConfig migrateLegacy();

    // Atomic write using QSaveFile (same pattern as T20 InstanceConfigManager).
    bool atomicWrite(const QString& path, const QByteArray& data) const;
};
