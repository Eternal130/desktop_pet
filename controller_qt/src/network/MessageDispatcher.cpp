#include "network/MessageDispatcher.hpp"

#include <spdlog/spdlog.h>

#include "logging/Logging.hpp"

MessageDispatcher::MessageDispatcher(QObject* parent)
    : QObject(parent) {}

void MessageDispatcher::setResponseHandler(std::function<void(const Envelope&)> handler) {
    m_responseHandler = std::move(handler);
}

void MessageDispatcher::registerEventHandler(const QString& action,
                                             std::function<void(const Envelope&)> handler) {
    m_eventHandlers.insert(action, std::move(handler));
}

void MessageDispatcher::unregisterEventHandler(const QString& action) {
    m_eventHandlers.remove(action);
}

void MessageDispatcher::dispatch(const Envelope& env) {
    // Route by env.type FIRST. This is the structural guarantee of §7.3: a
    // type=="event" envelope (incl. stats_state / layout_state) can never reach
    // the response handler, no matter what its id carries. The pseudo-response
    // id-collision case from event_all_13.json is handled correctly here without
    // any per-action special-casing.
    if (env.type == QStringLiteral("response")) {
        if (m_responseHandler) {
            m_responseHandler(env);
        } else {
            LOG_WARN("no response handler registered, dropping response (action='{}', id='{}')",
                     env.action.toStdString(), env.id.toStdString());
        }
        return;
    }

    if (env.type == QStringLiteral("event")) {
        auto it = m_eventHandlers.find(env.action);
        if (it == m_eventHandlers.end()) {
            LOG_WARN("no handler for action '{}', dropping event (id='{}')",
                     env.action.toStdString(), env.id.toStdString());
            return;
        }
        // Handler isolation (blueprint §7): catch any exception so a bad handler
        // cannot abort the message pump. The next inbound message still routes.
        try {
            it.value()(env);
        } catch (const std::exception& e) {
            LOG_ERROR("handler for action '{}' threw exception: {}",
                      env.action.toStdString(), e.what());
        }
        return;
    }

    if (env.type == QStringLiteral("command")) {
        // Controller is the WS Server — it SENDS commands to the renderer, it
        // never receives them. An inbound command means the peer is confused.
        LOG_WARN("received unexpected command type inbound (action='{}', id='{}')",
                 env.action.toStdString(), env.id.toStdString());
        return;
    }

    LOG_WARN("received message with unknown type '{}', dropping (action='{}')",
             env.type.toStdString(), env.action.toStdString());
}
