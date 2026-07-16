#pragma once

#include <QObject>
#include <QString>

// WindowStateSaver (task T28 / Phase 4.6) — the QML↔PanelConfig bridge that
// persists window geometry + theme across restarts.
//
// The QML ApplicationWindow cannot call PanelStateManager (a plain C++ class)
// directly, so this thin QObject exposes one Q_INVOKABLE: saveWindowState(x, y,
// width, height, theme). It uses a load-modify-save cycle: the current
// panel.json is read (so instanceIds + the other 7 PanelConfig fields survive
// unchanged), only the 5 window-state fields are overwritten, then the result
// is written atomically via PanelStateManager (QSaveFile pattern from T20).
//
// Registered as a QML context property "windowStateSaver" in main.cpp (same
// pattern as EnvironmentChecker — Q_OBJECT for Q_INVOKABLE, NO QML_ELEMENT, to
// avoid the type-registration resolution conflict noted in
// EnvironmentChecker.hpp). Main.qml calls windowStateSaver.saveWindowState(...)
// from its onClosing handler and from a Theme.themeChanged Connections block.
//
// Scope (task T28): persists ONLY panelX/Y/Width/Height + theme. The other
// PanelConfig fields (fontSize, panelOpacity, instanceIds, autoLaunchSystem,
// startMinimized, closeAction, confirmOnExit) are preserved verbatim through
// the load-modify-save cycle but are not mutated here — Phase 5 wires their UI.
class WindowStateSaver : public QObject
{
    Q_OBJECT

public:
    // configDir: the panel.json root (ConfigDir::configDir() in production, or
    // an injected temp path for tests). If empty, PanelStateManager falls back
    // to the real ConfigDir::configDir() itself — so passing the real path
    // explicitly and passing {} are equivalent in production.
    explicit WindowStateSaver(const QString& configDir = {},
                              QObject* parent = nullptr);

    // Load panel.json, overwrite panelX/Y/Width/Height + theme, save atomically.
    // Returns true on successful save. The other 7 PanelConfig fields are
    // preserved unchanged. Never throws — load/save failures are logged at WARN
    // and return false so a config-disk failure cannot crash the UI.
    //
    // Thread safety: designed to be called only from the GUI thread (QML
    // callbacks). PanelStateManager does synchronous file I/O; no locking needed.
    Q_INVOKABLE bool saveWindowState(int x, int y, int width, int height,
                                     const QString& theme);

private:
    QString m_configDir;
};
