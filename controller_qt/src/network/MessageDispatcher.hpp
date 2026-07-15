#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <functional>

#include "network/Envelope.hpp"

// Message Dispatcher (T7) — routes inbound WebSocket Envelopes:
//   - type=="response" → the single response handler (T10 wires this to the
//     pending-request table's lookup-and-complete logic; until set, responses
//     are logged + dropped).
//   - type=="event"    → the handler registered for env.action, if any.
//   - type=="command"  → LOG_WARN (controller sends commands, never receives).
//
// Two protocol rules (architecture-blueprint.md §7 + interface.md §7.3) shape
// the routing:
//   1. Handler isolation — an exception thrown inside any handler is caught and
//      logged; the NEXT inbound message must still route. A bad handler never
//      poisons the message pump.
//   2. Pseudo-response rule — stats_state / layout_state are EVENTS
//      (type=="event"), NOT responses. Their id is minted fresh by the renderer
//      and may even COLLIDE with a pending command id. The dispatcher routes
//      them by ACTION via the event-handler map, never by id. Routing keys off
//      env.type first, so a type=="event" envelope is never misrouted to the
//      response handler regardless of its id.
class MessageDispatcher : public QObject {
    Q_OBJECT
public:
    explicit MessageDispatcher(QObject* parent = nullptr);

    // Set the response handler invoked for every type=="response" Envelope.
    // T10 wires this to PendingRequests::handleResponse(). Until set,
    // responses are logged + dropped. Pass an empty/{} to clear.
    void setResponseHandler(std::function<void(const Envelope&)> handler);

    // Register an event handler for a specific action string. Re-registering
    // the same action replaces the handler.
    void registerEventHandler(const QString& action, std::function<void(const Envelope&)> handler);

    // Unregister an event handler. No-op if no handler is registered for action.
    void unregisterEventHandler(const QString& action);

public slots:
    // Route an inbound Envelope. Called by WsServer::messageReceived.
    void dispatch(const Envelope& env);

private:
    std::function<void(const Envelope&)> m_responseHandler;
    QMap<QString, std::function<void(const Envelope&)>> m_eventHandlers;
};
