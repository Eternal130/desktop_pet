#include "core/PanelConfigController.hpp"

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "core/EventCatalog.hpp"
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
    m_defaultModelName = cfg.defaultModelName;
    m_defaultGraphicsBackend = cfg.defaultGraphicsBackend;
    // S6: seed the kill-switch mirror from the SAME kv row the
    // PanelApplication provider reads (absent/"1" = enabled). Falls back
    // to enabled when no db is mounted.
    m_pluginWriteEnabled = m_db != nullptr
        ? m_db->getValue(QStringLiteral("plugin_write_enabled"),
                         QStringLiteral("1")) != QStringLiteral("0")
        : true;
    LOG_DEBUG("PanelConfigController loaded: closeAction=\"{}\" confirmOnExit={} "
              "startMinimized={} autoLaunchSystem={} defaultModelName=\"{}\" "
              "defaultGraphicsBackend=\"{}\" pluginWriteEnabled={}",
              m_closeAction.toStdString(), m_confirmOnExit,
              m_startMinimized, m_autoLaunchSystem,
              m_defaultModelName.toStdString(),
              m_defaultGraphicsBackend.toStdString(), m_pluginWriteEnabled);
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
//
// S6 (v1.3): the four whitelisted behavior fields WRITE through the
// injected pet::ISettingsApi (the same load-modify-save path plugins
// with the settings_write grant use). Validation moved WITH the write:
// the API answers InvalidArgument for illegal values (empty model name,
// non-exit/minimize/ask closeAction) and the controller keeps its cache + a
// WARN, exactly like the old inline guards. Null API (tests / degraded
// wiring) takes the legacy direct path below — byte-identical behavior.
//
// Fix 3 (silent-failure NOTIFY): every FAILED-API / FAILED-updateField
// early return emits the property's NOTIFY anyway. The cache was never
// changed, but the QML side HAS already flipped its visual state (two-way
// bindings write first, read back on NOTIFY) — without a NOTIFY the
// display keeps showing the user's rejected pick and looks saved. The
// extra signal re-evaluates bindings off the unchanged cache so the
// display reverts to the persisted value. Pre-validation guards
// (vocabulary / empty-name rejects) do NOT emit — existing tests assert
// those stay silent, and no write was attempted. Success paths are
// byte-identical to before.

void PanelConfigController::setCloseAction(const QString& action)
{
    if (m_closeAction == action) return;
    // B2 guard: only accept "exit", "minimize" or "ask". Any other value is
    // rejected (defensive — QML SegmentedControl only produces legal
    // values, but a corrupt panel.json or a future caller could pass
    // anything).
    if (action != QLatin1String("exit") && action != QLatin1String("minimize")
        && action != QLatin1String("ask")) {
        LOG_WARN("PanelConfigController::setCloseAction: rejecting invalid value "
                 "'{}' (only \"exit\"/\"minimize\"/\"ask\" allowed)",
                 action.toStdString());
        return;
    }
    if (m_settingsApi != nullptr) {
        if (m_settingsApi->setCloseAction(action) != pet::PluginError::Ok) {
            LOG_WARN("PanelConfigController: settingsApi setCloseAction failed");
            emit closeActionChanged(); // Fix 3: revert display to cache
            return;
        }
    } else if (!updateField([action](PanelConfig& cfg) { cfg.closeAction = action; })) {
        emit closeActionChanged(); // Fix 3: revert display to cache
        return;
    }
    const QString old = m_closeAction;
    m_closeAction = action;
    emit closeActionChanged();
    LOG_INFO("PanelConfig: closeAction \"{}\" -> \"{}\"",
             old.toStdString(), action.toStdString());
}

void PanelConfigController::setConfirmOnExit(bool enabled)
{
    if (m_confirmOnExit == enabled) return;
    if (m_settingsApi != nullptr) {
        if (m_settingsApi->setConfirmOnExit(enabled) != pet::PluginError::Ok) {
            emit confirmOnExitChanged(); // Fix 3: revert display to cache
            return;
        }
    } else if (!updateField([enabled](PanelConfig& cfg) { cfg.confirmOnExit = enabled; })) {
        emit confirmOnExitChanged(); // Fix 3: revert display to cache
        return;
    }
    m_confirmOnExit = enabled;
    emit confirmOnExitChanged();
    LOG_INFO("PanelConfig: confirmOnExit -> {}", enabled);
}

void PanelConfigController::setStartMinimized(bool enabled)
{
    if (m_startMinimized == enabled) return;
    if (m_settingsApi != nullptr) {
        if (m_settingsApi->setStartMinimized(enabled) != pet::PluginError::Ok) {
            emit startMinimizedChanged(); // Fix 3: revert display to cache
            return;
        }
    } else if (!updateField([enabled](PanelConfig& cfg) { cfg.startMinimized = enabled; })) {
        emit startMinimizedChanged(); // Fix 3: revert display to cache
        return;
    }
    m_startMinimized = enabled;
    emit startMinimizedChanged();
    LOG_INFO("PanelConfig: startMinimized -> {}", enabled);
}

void PanelConfigController::setAutoLaunchSystem(bool enabled)
{
    // ARCHITECTURE DECISION (S6): autoLaunchSystem deliberately stays on
    // the DIRECT host path — OS-level autostart is a host-shell
    // capability (registry / .desktop mutation) and is intentionally NOT
    // part of pet::ISettingsApi (see its header). Never forwarded.
    // v1.9: the WRITE path is unchanged; a successful persist now ALSO
    // publishes pet.panel.settings_changed (post-commit — the QML toggle
    // drives AutoLaunchManager::enable()/disable() immediately BEFORE
    // this property write, so the emission lands after both the OS action
    // and the panel_config persist).
    if (m_autoLaunchSystem == enabled) return;
    m_autoLaunchSystem = enabled;
    if (!updateField([enabled](PanelConfig& cfg) { cfg.autoLaunchSystem = enabled; })) {
        m_autoLaunchSystem = !enabled;
        emit autoLaunchSystemChanged(); // Fix 3: revert display to cache
        return;
    }
    emit autoLaunchSystemChanged();
    LOG_INFO("PanelConfig: autoLaunchSystem -> {}", enabled);
    publishSettingsChanged(QStringLiteral("auto_launch_system"), enabled);
}

void PanelConfigController::setDefaultModelName(const QString& name)
{
    // Free-form string (see the Q_PROPERTY note) — no enum-style guard here;
    // an empty name is rejected (falls back to keeping the current value)
    // because the createInstance flow treats empty as "use the default".
    if (m_defaultModelName == name) return;
    if (name.isEmpty()) {
        LOG_WARN("PanelConfigController::setDefaultModelName: rejecting empty name");
        return;
    }
    if (m_settingsApi != nullptr) {
        if (m_settingsApi->setDefaultModelName(name) != pet::PluginError::Ok) {
            LOG_WARN("PanelConfigController: settingsApi setDefaultModelName failed");
            emit defaultModelNameChanged(); // Fix 3: revert display to cache
            return;
        }
    } else if (!updateField([name](PanelConfig& cfg) { cfg.defaultModelName = name; })) {
        emit defaultModelNameChanged(); // Fix 3: revert display to cache
        return;
    }
    const QString old = m_defaultModelName;
    m_defaultModelName = name;
    emit defaultModelNameChanged();
    LOG_INFO("PanelConfig: defaultModelName \"{}\" -> \"{}\"",
             old.toStdString(), name.toStdString());
}

void PanelConfigController::setDefaultGraphicsBackend(const QString& backend)
{
    // v1.10: enum-style vocabulary guard (the setCloseAction discipline,
    // case-insensitive + normalized to lowercase so the cache and the
    // persisted value always hold the canonical spelling).
    const QString normalized = backend.toLower();
    if (m_defaultGraphicsBackend == normalized) return;
    if (normalized != QLatin1String("opengl")
        && normalized != QLatin1String("vulkan")) {
        LOG_WARN("PanelConfigController::setDefaultGraphicsBackend: rejecting "
                 "invalid value '{}' (only \"opengl\"/\"vulkan\" allowed)",
                 backend.toStdString());
        return;
    }
    if (m_settingsApi != nullptr) {
        if (m_settingsApi->setDefaultGraphicsBackend(normalized)
                != pet::PluginError::Ok) {
            LOG_WARN("PanelConfigController: settingsApi "
                     "setDefaultGraphicsBackend failed");
            emit defaultGraphicsBackendChanged(); // Fix 3: revert display
            return;
        }
    } else if (!updateField([normalized](PanelConfig& cfg) {
                   cfg.defaultGraphicsBackend = normalized;
               })) {
        emit defaultGraphicsBackendChanged(); // Fix 3: revert display
        return;
    }
    const QString old = m_defaultGraphicsBackend;
    m_defaultGraphicsBackend = normalized;
    emit defaultGraphicsBackendChanged();
    LOG_INFO("PanelConfig: defaultGraphicsBackend \"{}\" -> \"{}\"",
             old.toStdString(), normalized.toStdString());
}

void PanelConfigController::setPluginWriteEnabled(bool enabled)
{
    // S6: writes the DEDICATED kv row (never the panel_config blob) so
    // PanelApplication's live provider flips on the very next queryApi.
    if (m_pluginWriteEnabled == enabled) return;
    if (m_db == nullptr) {
        LOG_WARN("PanelConfigController::setPluginWriteEnabled: no shared "
                 "DatabaseManager — kv row not written");
        emit pluginWriteEnabledChanged(); // Fix 3: revert display to cache
        return;
    }
    if (!m_db->setValue(QStringLiteral("plugin_write_enabled"),
                        enabled ? QStringLiteral("1") : QStringLiteral("0"))) {
        LOG_WARN("PanelConfigController: plugin_write_enabled write failed");
        emit pluginWriteEnabledChanged(); // Fix 3: revert display to cache
        return;
    }
    m_pluginWriteEnabled = enabled;
    emit pluginWriteEnabledChanged();
    LOG_INFO("PanelConfig: pluginWriteEnabled -> {}", enabled);
}

void PanelConfigController::publishSettingsChanged(const QString& field,
                                                    const QJsonValue& value)
{
    // v1.9: the host-direct autoLaunchSystem twin of
    // SettingsApiImpl::publishSettingsChanged — pet.panel.settings_changed
    // joins the SAME channel the ISettingsApi writes publish on
    // (PANEL-LEVEL: instanceUuid stays ""; pre-v1.9 the autoLaunch flip
    // was the one documented gap, see docs/bus-events.md §9's revision
    // record). Payload via the EventCatalog builder (keys never
    // hand-written at emission sites).
    if (m_eventPublisher == nullptr)
        return; // tests / degraded wiring — pre-v1.9 behavior
    pet::Event evt;
    evt.eventId = QString(pet::kEventPanelSettingsChanged);
    evt.instanceUuid = QString(); // panel-level
    evt.publisherId = QString(pet::kEventPublisherHost);
    evt.cancellable = false;
    evt.payload = core::makeSettingsChangedPayload(field, value);
    m_eventPublisher->publish(evt); // synchronous; errors are bus-level
}
