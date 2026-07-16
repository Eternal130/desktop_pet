#include "core/WindowStateSaver.hpp"

#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding in
// .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

WindowStateSaver::WindowStateSaver(const QString& configDir, QObject* parent)
    : QObject(parent), m_configDir(configDir)
{
}

bool WindowStateSaver::saveWindowState(int x, int y, int width, int height,
                                       const QString& theme)
{
    // Load-modify-save: read the current panel.json so the 7 fields this saver
    // does NOT own (fontSize, panelOpacity, instanceIds, autoLaunchSystem,
    // startMinimized, closeAction, confirmOnExit) survive the write unchanged,
    // then overwrite only the 5 window-state fields and persist atomically.
    // PanelStateManager handles the fresh-install case (writes defaults on the
    // internal load()) and the atomic QSaveFile commit (T20 pattern).
    PanelStateManager psm(m_configDir);
    PanelConfig cfg = psm.load();
    cfg.panelX = x;
    cfg.panelY = y;
    cfg.panelWidth = width;
    cfg.panelHeight = height;
    cfg.theme = theme;

    if (!psm.save(cfg)) {
        // psm.save() already logged the specific write/rename failure at WARN;
        // surface the outcome to QML so a future Settings page could show it.
        LOG_WARN("saveWindowState: panel.json save failed (x={} y={} {}x{})",
                 x, y, width, height);
        return false;
    }
    LOG_INFO("Saved window state: x={} y={} {}x{} theme=\"{}\"",
             x, y, width, height, theme.toStdString());
    return true;
}
