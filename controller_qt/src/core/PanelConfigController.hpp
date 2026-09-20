#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "core/DatabaseManager.hpp"

struct PanelConfig;

// PanelConfigController (Wave 7 todo 15) — the QML bridge for the 4
// startup/exit-behavior fields of PanelConfig that SettingsPage.qml edits:
// closeAction, confirmOnExit, startMinimized, autoLaunchSystem.
//
// Exposes these as Q_PROPERTY (READ + WRITE + NOTIFY) so QML can bind to them
// with the standard two-way pattern (`checked: panelConfig.confirmOnExit` +
// `onToggled: panelConfig.confirmOnExit = checked`). Each setter does a
// load-modify-save via PanelStateManager so the write is atomic (QSaveFile)
// and the other 8 PanelConfig fields survive unchanged — the same discipline
// as WindowStateSaver.
//
// Registered as the QML context property "panelConfig" in main.cpp (same
// pattern as windowStateSaver / envChecker — Q_OBJECT for Q_PROPERTY + NO
// QML_ELEMENT, to avoid the type-registration resolution conflict noted in
// EnvironmentChecker.hpp).
//
// Scope: owns ONLY the 4 behavior fields. Window geometry + theme go through
// WindowStateSaver (T28); the instance roster through InstanceManager (todo 3).
// The PanelConfig struct itself stays a plain C++ struct (no Q_OBJECT) — this
// controller is the QML adapter, NOT the data model.
class PanelConfigController : public QObject
{
    Q_OBJECT
    // B2 guard: closeAction is "exit" or "minimize" (NOT "hide_to_tray" —
    // that was the legacy Java value; PanelConfig.hpp:48 uses "minimize").
    Q_PROPERTY(QString closeAction     READ closeAction     WRITE setCloseAction     NOTIFY closeActionChanged)
    Q_PROPERTY(bool     confirmOnExit  READ confirmOnExit  WRITE setConfirmOnExit  NOTIFY confirmOnExitChanged)
    Q_PROPERTY(bool     startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged)
    Q_PROPERTY(bool     autoLaunchSystem READ autoLaunchSystem WRITE setAutoLaunchSystem NOTIFY autoLaunchSystemChanged)
    // Model-library setting: the model new instances get by default.
    // Free-form string (a dir name under Resources/Models/) — validity is
    // the model-library UI's concern (it offers the scanned list); an
    // unknown name simply fails at renderer launch with model_load_failed.
    Q_PROPERTY(QString defaultModelName READ defaultModelName WRITE setDefaultModelName NOTIFY defaultModelNameChanged)

public:
    // configDir: the panel.json root (ConfigDir::configDir() in production, or
    // an injected temp path for tests). If empty, PanelStateManager falls back
    // to the real ConfigDir::configDir() itself — so passing the real path
    // explicitly and passing {} are equivalent in production.
    explicit PanelConfigController(const QString& configDir = {},
                                   QObject* parent = nullptr);

    // Share main.cpp's DatabaseManager (SQLite backend). Optional — without
    // it, each load-modify-save opens its own connection at <configDir>/app.db.
    void setDatabase(DatabaseManager* db) { m_db = db; }

    // ── READ accessors (backed by m_cache, loaded once in ctor) ───────────
    QString closeAction() const     { return m_closeAction; }
    bool    confirmOnExit() const   { return m_confirmOnExit; }
    bool    startMinimized() const  { return m_startMinimized; }
    bool    autoLaunchSystem() const { return m_autoLaunchSystem; }
    QString defaultModelName() const { return m_defaultModelName; }

    // ── WRITE accessors (mutate cache + persist atomically + emit NOTIFY) ─
    // Each does load-modify-save: read the current panel.json, overwrite the
    // one field, write back via PanelStateManager (QSaveFile). No-op (no
    // emission, no disk write) when the new value equals the cached value —
    // avoids spurious NOTIFY signals + redundant I/O on a same-value write.
    void setCloseAction(const QString& action);
    void setConfirmOnExit(bool enabled);
    void setStartMinimized(bool enabled);
    void setAutoLaunchSystem(bool enabled);
    void setDefaultModelName(const QString& name);

signals:
    void closeActionChanged();
    void confirmOnExitChanged();
    void startMinimizedChanged();
    void autoLaunchSystemChanged();
    void defaultModelNameChanged();

private:
    // Load panel.json once at construction → seed the 4 cached fields. Used
    // only in the ctor; subsequent reads come from the cache (kept in sync
    // with disk by the setters). A missing/corrupt file seeds defaults
    // (PanelStateManager::load handles fresh-install + corrupt-file paths).
    void loadFromDisk();

    // Load-modify-save helper: reads the current PanelConfig from disk,
    // applies `mutator`, writes it back. Returns true on successful save.
    // Used by every setter so the other 8 fields always survive the write.
    bool updateField(std::function<void(PanelConfig&)> mutator);

    QString m_configDir;
    DatabaseManager* m_db = nullptr;
    QString m_closeAction;
    bool    m_confirmOnExit = false;
    bool    m_startMinimized = false;
    bool    m_autoLaunchSystem = false;
    QString m_defaultModelName;
};
