#include "core/PanelConfigController.hpp"

#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

PanelConfigController::PanelConfigController(const QString& configDir,
                                             QObject* parent)
    : QObject(parent), m_configDir(configDir)
{
    loadFromDisk();
}

void PanelConfigController::loadFromDisk()
{
    // Seed the 4 cached fields from the persisted panel.json. A fresh install
    // / corrupt file yields the PanelConfig defaults (closeAction="exit",
    // confirmOnExit=false, startMinimized=false, autoLaunchSystem=false) —
    // which match the Q_PROPERTY defaults, so the QML bindings render the
    // correct initial state without any post-construction signaling.
    PanelStateManager psm(m_configDir);
    if (m_db != nullptr)
        psm.setDatabase(m_db);
    const PanelConfig cfg = psm.load();
    m_closeAction = cfg.closeAction;
    m_confirmOnExit = cfg.confirmOnExit;
    m_startMinimized = cfg.startMinimized;
    m_autoLaunchSystem = cfg.autoLaunchSystem;
    LOG_DEBUG("PanelConfigController loaded: closeAction=\"{}\" confirmOnExit={} "
              "startMinimized={} autoLaunchSystem={}",
              m_closeAction.toStdString(), m_confirmOnExit,
              m_startMinimized, m_autoLaunchSystem);
}

bool PanelConfigController::updateField(std::function<void(PanelConfig&)> mutator)
{
    // Load-modify-save: read the current panel.json so the 8 fields this
    // controller does NOT own (geometry, theme, fontSize, panelOpacity,
    // instanceIds) survive the write unchanged, then overwrite only the one
    // field the setter touched, and persist atomically via PanelStateManager
    // (QSaveFile — T20 pattern). Mirrors WindowStateSaver::saveWindowState.
    PanelStateManager psm(m_configDir);
    if (m_db != nullptr)
        psm.setDatabase(m_db);
    PanelConfig cfg = psm.load();
    mutator(cfg);
    if (!psm.save(cfg)) {
        // psm.save() already logged the specific write/rename failure at WARN.
        LOG_WARN("PanelConfigController: panel.json save failed");
        return false;
    }
    return true;
}

// ── Setters ────────────────────────────────────────────────────────────────
// Each: no-op on same value (avoids spurious NOTIFY + disk I/O), otherwise
// update cache + persist + emit NOTIFY so QML bindings re-evaluate.

void PanelConfigController::setCloseAction(const QString& action)
{
    if (m_closeAction == action) return;
    // B2 guard: only accept "exit" or "minimize". Any other value is logged
    // at WARN + silently ignored (defensive — QML SegmentedControl only
    // produces these two values, but a corrupt panel.json or a future caller
    // could pass anything).
    if (action != QLatin1String("exit") && action != QLatin1String("minimize")) {
        LOG_WARN("PanelConfigController::setCloseAction: rejecting invalid value "
                 "'{}' (only \"exit\"/\"minimize\" allowed)",
                 action.toStdString());
        return;
    }
    const QString old = m_closeAction;
    m_closeAction = action;
    if (!updateField([action](PanelConfig& cfg) { cfg.closeAction = action; })) {
        // Roll back the cache on persistence failure so QML reads the value
        // that's actually on disk (the old one).
        m_closeAction = old;
        return;
    }
    emit closeActionChanged();
    LOG_INFO("PanelConfig: closeAction \"{}\" -> \"{}\"",
             old.toStdString(), action.toStdString());
}

void PanelConfigController::setConfirmOnExit(bool enabled)
{
    if (m_confirmOnExit == enabled) return;
    m_confirmOnExit = enabled;
    if (!updateField([enabled](PanelConfig& cfg) { cfg.confirmOnExit = enabled; })) {
        m_confirmOnExit = !enabled;  // roll back
        return;
    }
    emit confirmOnExitChanged();
    LOG_INFO("PanelConfig: confirmOnExit -> {}", enabled);
}

void PanelConfigController::setStartMinimized(bool enabled)
{
    if (m_startMinimized == enabled) return;
    m_startMinimized = enabled;
    if (!updateField([enabled](PanelConfig& cfg) { cfg.startMinimized = enabled; })) {
        m_startMinimized = !enabled;
        return;
    }
    emit startMinimizedChanged();
    LOG_INFO("PanelConfig: startMinimized -> {}", enabled);
}

void PanelConfigController::setAutoLaunchSystem(bool enabled)
{
    if (m_autoLaunchSystem == enabled) return;
    m_autoLaunchSystem = enabled;
    if (!updateField([enabled](PanelConfig& cfg) { cfg.autoLaunchSystem = enabled; })) {
        m_autoLaunchSystem = !enabled;
        return;
    }
    emit autoLaunchSystemChanged();
    LOG_INFO("PanelConfig: autoLaunchSystem -> {}", enabled);
}
