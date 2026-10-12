#pragma once

// EventBus (S8a, v1.5) — the Forge-style in-process event bus core behind
// the pet::IEventApi family (queryApi "pet.events").
//
// Mechanics (spec-frozen):
//   - subscription table: eventId → listener entries sorted by priority
//     VALUE ascending (Highest first), stable within one priority
//     (same-priority FIFO — std::stable_sort on append);
//   - subscribe() validates the eventId syntax (isValidEventId below —
//     ≥3 dot-separated non-empty segments, [a-z0-9_] only, no
//     wildcards) and rejects violations with InvalidArgument; a null
//     listener is InvalidArgument; re-subscribing an existing
//     (eventId, listener) pair MERGES (priority + publisherId updated
//     in place, no duplicate entry);
//   - publish() stamps timestampMs, validates the eventId (zero
//     dispatch on violation), then iterates a SNAPSHOT copy of the
//     listener vector — listeners may subscribe/unsubscribe/re-publish
//     from inside onEvent without invalidating the iteration;
//   - every onEvent call point is wrapped in try/catch(...) (§A.1
//     fanout isolation — a throwing listener is logged and the fanout
//     continues, mirroring PluginContextImpl's observer discipline);
//   - re-entrancy: a per-thread depth counter caps nested publish()
//     chains; the publish that would exceed the cap returns Busy with a
//     WARN (a runaway listener feedback loop cannot hang the GUI
//     thread);
//   - non-cancellable events: a listener writing event.cancelled is
//     ignored — the bus resets the flag after every listener call, so
//     the final value at publish return is false (no error is raised);
//   - publisherId bookkeeping: every entry records WHO subscribed (host
//     channel = "", plugin channel = the plugin id via EventApiProxy),
//     powering unsubscribeByPublisher() — the plugin-unload sweep that
//     guarantees a misbehaving plugin cannot leave dangling listener
//     pointers behind.
//
// publish() does NOT overwrite publisherId: the shared EventApiImpl
// forwards verbatim (the host emitter owns the field), while the
// per-plugin EventApiProxy force-stamps its own id before forwarding.
//
// QObject so it slots into the PanelApplication service tree (lifetime
// by registration order); no signals of its own — dispatch is
// synchronous and returns through publish().
//
// Threading: GUI thread only (the whole family's contract); the depth
// counter is thread_local regardless, so a future multi-threaded caller
// cannot share re-entrancy state across threads.

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include "api/IEventApi.hpp"

namespace core {

// Re-entrancy cap shared by every EventBus instance (per-thread): the
// publish occupying depth kEventMaxPublishDepth returns Busy.
inline constexpr int kEventMaxPublishDepth = 8;

class EventBus : public QObject
{
    Q_OBJECT

public:
    explicit EventBus(QObject* parent = nullptr);

    // Event ID syntax check (api/EventIds.hpp rules): at least
    // kEventIdMinSegments dot-separated segments, every segment
    // non-empty, characters limited to [a-z0-9_] and the separating
    // dots. Wildcards ('*'), uppercase, and any other character fail.
    // Pure function — shared by subscribe()/publish() here and by
    // EventApiProxy's prefix pre-checks. Never throws.
    static bool isValidEventId(const QString& eventId);

    // Register one listener. See the class comment for the merge +
    // validation rules. `publisherId` is bookkeeping only ("" = host
    // channel); it never influences dispatch.
    pet::PluginError subscribe(const QString& eventId,
                               pet::IEventListener* listener,
                               pet::EventPriority priority,
                               const QString& publisherId);

    // Remove the (eventId, listener) pair; no-op when absent.
    void unsubscribe(const QString& eventId, pet::IEventListener* listener);

    // Synchronous snapshot dispatch. See the class comment for the
    // stamping / isolation / depth / cancellation rules. The event's
    // publisherId is forwarded verbatim.
    pet::PluginError publish(pet::Event& event);

    // Plugin-unload sweep: drop EVERY subscription booked under
    // `publisherId` (across all eventIds). Returns the number of
    // entries removed — PluginHost WARNs when a plugin needed the sweep
    // (it should have unsubscribed in shutdown() itself).
    int unsubscribeByPublisher(const QString& publisherId);

    // Test observer: total entries across all channels.
    int subscriptionCount() const;

private:
    // One live subscription. Flat value type; the listener is NOT owned.
    struct Entry {
        pet::IEventListener* listener = nullptr;
        pet::EventPriority priority = pet::EventPriority::Normal;
        QString publisherId; // "" = host channel; else the plugin id
    };

    // eventId → entries, kept sorted by priority value (stable).
    QHash<QString, QVector<Entry>> m_subscriptions;
};

} // namespace core
