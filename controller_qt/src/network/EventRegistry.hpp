#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "network/Envelope.hpp"

class MessageDispatcher; // forward decl — full include in EventRegistry.cpp

// EventRegistry (T11) — thin convenience wrapper over MessageDispatcher (T7)
// that gives the application layer a single, well-named seam for wiring up
// event handlers. Every method delegates to the dispatcher; NO routing logic
// lives here.
//
// The two protocol events stats_state and layout_state are pseudo-responses
// (interface.md §7.3): the renderer mints a fresh id for them and that id MAY
// collide with a pending command id. MessageDispatcher routes by env.type
// first, so a type=="event" envelope always reaches the action-keyed handler
// regardless of id — EventRegistry inherits that guarantee for free.
//
// registerDefaults() installs a default log-only handler for ALL 13 events
// from interface.md §4.1, so an event the controller does not yet handle is
// still observed (not silently dropped with a WARN). The caller is expected to:
//   1. call registerDefaults() once at startup, THEN
//   2. OVERRIDE specific events via registerEventHandler() (QMap::insert
//      replaces, so the override always wins regardless of call order).
class EventRegistry : public QObject {
    Q_OBJECT
public:
    explicit EventRegistry(MessageDispatcher* dispatcher, QObject* parent = nullptr);

    // Register a handler for a specific event action.
    // Delegates to MessageDispatcher::registerEventHandler (re-registering the
    // same action replaces the handler).
    void registerEventHandler(const QString& action,
                              std::function<void(const Envelope&)> handler);

    // Unregister a handler. No-op if no handler is registered for action.
    // Delegates to MessageDispatcher::unregisterEventHandler.
    void unregisterEventHandler(const QString& action);

    // Convenience: register default log-only handlers for ALL 13 protocol events
    // (interface.md §4.1). Phase 0-4 active events log at INFO; Phase 6+ events
    // (hit, motion_started, motion_finished, drag_start) log at DEBUG.
    //
    // Call this BEFORE registering custom handlers; a later
    // registerEventHandler() for the same action replaces the default. This
    // method does not consult any "already registered?" state because the
    // underlying dispatcher has no such query — the override semantics of
    // QMap::insert make the call-order contract sufficient.
    void registerDefaults();

private:
    MessageDispatcher* m_dispatcher;
};
