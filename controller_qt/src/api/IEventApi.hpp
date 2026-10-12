#pragma once

// Plugin SDK — Forge-style event-bus family (S8a, v1.5).
//
// Post-freeze interface family: derives pet::IExtApi and is obtained
// EXCLUSIVELY through IPluginContext::queryApi(kEventsApiId, minVersion).
// NOT gated — neither by any manifest capability nor by the host's
// plugin_write_enabled kill-switch (the switch revokes WRITES to host
// state; the bus is a notification channel, and the plugin-side forgery
// protection is the per-context proxy's publisher-prefix rule, not a
// capability). When the host has no shared implementation wired the
// query returns nullptr ("feature absent").
//
// SYNCHRONOUS DISPATCH SEMANTICS: publish() returns only after the ENTIRE
// listener chain for that event has run; at return time event.cancelled
// holds its final value. There is no queue, no thread hop, no async
// delivery — a listener that blocks blocks the GUI thread (§B.3), so
// listeners must be fast in-and-out.
//
// Subscription contract:
//   - subscribing to an UNKNOWN eventId is LEGAL (subscribe-before-
//     publish; the host may light a channel up later);
//   - subscribing the same (eventId, listener) pair again MERGES: the
//     entry is not duplicated, the priority is updated in place;
//   - a plugin MUST unsubscribe every listener before shutdown()
//     returns (the host sweeps leftovers at unload with a WARN — the
//     listener lifetime is ALWAYS the plugin's; the host never deletes
//     a listener);
//   - listeners may unsubscribe (themselves or others) from inside
//     onEvent — the bus iterates a snapshot, so removal mid-fanout is
//     safe; listeners still in the snapshot receive exactly one call.
//
// Publish contract (anti-forgery, "one domain one channel"):
//   - a PLUGIN may publish ONLY "plugin.<ownId>.*" channels (enforced
//     by the per-context proxy: anything else returns
//     PluginError::InvalidArgument with ZERO dispatch);
//   - the HOST may publish any syntactically legal channel (pet.* is
//     the host vocabulary — see api/EventIds.hpp for the frozen table);
//   - the proxy force-stamps publisherId with the plugin's own id; the
//     shared implementation never overwrites publisherId (the host
//     emitter owns it);
//   - re-entrant publish depth is capped (per-thread): a listener that
//     publishes nested events deeper than the cap gets
//     PluginError::Busy for the overflow publish (see EventBus) —
//     runaway feedback loops cannot hang the GUI thread.
//
// Cancellation contract:
//   - only events published with cancellable=true honor
//     event.cancelled = true written by a listener (later listeners
//     still receive the event — cancellation shortens nothing mid-chain
//     in v1.5; the poster reads the final value after publish returns);
//   - a listener writing cancelled on a non-cancellable event is
//     IGNORED (no error, the flag is reset before the next listener).
//
// Payload discipline: payloads are plain QJsonObject with snake_case
// keys; fields are add-only, absent values use sentinels (-1 / "") —
// see api/EventIds.hpp's field tables. The api/ layer passes QJsonObject
// by value/const-ref only (no std:: types, no exceptions, no
// std::function across the boundary).
//
// Threading contract: GUI thread only, never blocking.
//
// ABI discipline: same rules as every api/ header — pure virtual
// declarations + Qt value types only, no std:: across the boundary, no
// inline function bodies (the defaulted destructor exemption applies;
// Event's defaulted member initializers are data, not code — the same
// exemption MonitorSample's initializers rely on).

#include <QJsonObject>
#include <QString>
#include <QtGlobal> // qint64

#include "api/IPluginContext.hpp"
#include "api/PluginTypes.hpp"

namespace pet {

// apiId + per-family version for queryApi(). Version 1 = the surface
// below. minVersion > this → queryApi returns nullptr ("feature
// absent", never an error).
inline constexpr char kEventsApiId[] = "pet.events";
inline constexpr int kEventsApiVersion = 1;

// Delivery order within one channel: ascending priority VALUE (Highest
// first). Same-priority listeners fire in subscription order (FIFO,
// stable). The enum's underlying type is fixed so adding values stays
// ABI-safe (same rule as PluginError).
enum class EventPriority : unsigned int {
    Highest = 0,
    High = 1,
    Normal = 2,
    Low = 3,
    Lowest = 4,
};

// One bus message. Value type — copied across the boundary, never
// referenced. The publisher fills eventId/instanceUuid/publisherId/
// cancellable/payload; the bus stamps timestampMs at publish (the
// emitter's value is overwritten) and cancelled reaches its final value
// by the time publish returns.
struct Event {
    QString eventId;                     // channel (syntax: EventIds.hpp)
    QString instanceUuid;                // "" = panel-level event
    QString publisherId;                 // "host" or the plugin id
    qint64 timestampMs = 0;              // stamped by the bus
    bool cancellable = false;            // whether cancelled is honored
    bool cancelled = false;              // final value at publish return
    QJsonObject payload;                 // snake_case, add-only fields
};

// Synchronous listener. onEvent runs on the GUI thread, in the middle of
// the publisher's own call stack — fast in-and-out, never blocking, no
// re-entrant heavy work. Exceptions must not escape (the bus isolates a
// throwing listener and keeps fanning out, but a plugin should not rely
// on it).
class IEventListener
{
public:
    virtual ~IEventListener() = default;

    virtual void onEvent(Event& event) = 0;
};

class IEventApi : public IExtApi
{
public:
    // Register listener for eventId at priority. Ok on success;
    // InvalidArgument for a null listener or a syntactically illegal
    // eventId. Subscribing an unknown eventId is legal (subscribe before
    // the host ever publishes it). Re-subscribing the same
    // (eventId, listener) merges: priority updated, no duplicate entry.
    virtual PluginError subscribe(const QString& eventId,
                                  IEventListener*,
                                  EventPriority) = 0;

    // Remove the (eventId, listener) pair. No-op when never subscribed
    // (also the legal shape for a plugin that never subscribed).
    virtual void unsubscribe(const QString& eventId, IEventListener*) = 0;

    // Synchronously dispatch event to the channelId's listener chain
    // (snapshot iteration, per-listener exception isolation). Stamps
    // timestampMs. Returns Ok after the full chain ran (event.cancelled
    // is then final); InvalidArgument for an illegal eventId (zero
    // dispatch); Busy when the per-thread re-entrancy cap would be
    // exceeded. The per-plugin proxy additionally enforces the
    // plugin.<ownId>.* prefix (InvalidArgument, zero dispatch) and
    // force-stamps publisherId.
    virtual PluginError publish(Event& event) = 0;
};

} // namespace pet
