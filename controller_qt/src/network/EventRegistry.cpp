#include "network/EventRegistry.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <spdlog/spdlog.h>

#include "logging/Logging.hpp"
#include "network/MessageDispatcher.hpp"

namespace {

// Phase 0-4 active events (interface.md §4.1) — the controller is expected to
// install real handlers for these during Phase 0-4. Until it does, the default
// logs at INFO so an unhandled-but-expected event is visible in production.
const QString kActiveEvents[] = {
    QStringLiteral("ready"),
    QStringLiteral("model_loaded"),
    QStringLiteral("model_load_failed"),
    QStringLiteral("drag_end"),
    QStringLiteral("layout_changed"),
    QStringLiteral("window_resized"),
    QStringLiteral("stats_state"),
    QStringLiteral("layout_state"),
    QStringLiteral("error"),
};

// Phase 6+ events — interaction/motion telemetry the controller does not yet
// act on. Default logs at DEBUG: live in dev (SPDLOG_ACTIVE_LEVEL=DEBUG),
// stripped in release builds. No business logic until Phase 6.
const QString kPhase6Events[] = {
    QStringLiteral("hit"),
    QStringLiteral("motion_started"),
    QStringLiteral("motion_finished"),
    QStringLiteral("drag_start"),
};

// Compact JSON for one-line logging. Empty payload serializes as "{}".
QString payloadSummary(const QJsonObject& payload) {
    return QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

} // namespace

EventRegistry::EventRegistry(MessageDispatcher* dispatcher, QObject* parent)
    : QObject(parent), m_dispatcher(dispatcher) {}

void EventRegistry::registerEventHandler(
    const QString& action, std::function<void(const Envelope&)> handler) {
    m_dispatcher->registerEventHandler(action, std::move(handler));
}

void EventRegistry::unregisterEventHandler(const QString& action) {
    m_dispatcher->unregisterEventHandler(action);
}

void EventRegistry::registerDefaults() {
    // Capture action by value: each lambda owns its own copy of the action
    // string, so the handler is self-describing even if the static arrays were
    // ever moved (they are not, but the copy is cheap and removes doubt).
    for (const QString& action : kActiveEvents) {
        m_dispatcher->registerEventHandler(
            action,
            [action](const Envelope& env) {
                LOG_INFO("event action='{}' payload={}",
                         action.toStdString(),
                         payloadSummary(env.payload).toStdString());
            });
    }
    for (const QString& action : kPhase6Events) {
        m_dispatcher->registerEventHandler(
            action,
            [action](const Envelope& env) {
                LOG_DEBUG("event action='{}' payload={}",
                          action.toStdString(),
                          payloadSummary(env.payload).toStdString());
            });
    }
}
