#include "core/EventBus.hpp"

#include <QDateTime>

#include <algorithm>

#include <spdlog/spdlog.h>

#include "api/EventIds.hpp"
#include "logging/Logging.hpp"

namespace core {

namespace {

// Per-thread re-entrancy depth (shared by every EventBus instance — a
// listener can only re-enter through SOME bus, and one counter keeps the
// cap meaningful across the shared impl / per-host fallback pair).
thread_local int t_publishDepth = 0;

} // namespace

EventBus::EventBus(QObject* parent)
    : QObject(parent)
{
}

bool EventBus::isValidEventId(const QString& eventId)
{
    // ≥kEventIdMinSegments dot-separated segments, each non-empty, each
    // character in [a-z0-9_]. Uppercase, wildcards ('*') and every other
    // character fail; leading/trailing/duplicated dots create empty
    // segments and fail. Never throws.
    if (eventId.isEmpty())
        return false;
    int segments = 1;
    bool segmentEmpty = true; // becomes the leading-dot detector too
    for (const QChar& c : eventId) {
        const char16_t u = c.unicode();
        if (u == u'.') {
            if (segmentEmpty)
                return false; // "..", leading dot
            ++segments;
            segmentEmpty = true;
            continue;
        }
        if ((u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9')
            || u == u'_') {
            segmentEmpty = false;
            continue;
        }
        return false; // uppercase, '*', '-', spaces, non-ASCII, ...
    }
    if (segmentEmpty)
        return false; // trailing dot
    return segments >= pet::kEventIdMinSegments;
}

pet::PluginError EventBus::subscribe(const QString& eventId,
                                     pet::IEventListener* listener,
                                     pet::EventPriority priority,
                                     const QString& publisherId)
{
    if (listener == nullptr) {
        LOG_WARN("[event-bus] subscribe with a null listener — "
                 "ERR_INVALID_ARGUMENT");
        return pet::PluginError::InvalidArgument;
    }
    if (!isValidEventId(eventId)) {
        LOG_WARN("[event-bus] subscribe with illegal event id \"{}\" "
                 "(need ≥{} dot-separated [a-z0-9_] segments, no "
                 "wildcards) — ERR_INVALID_ARGUMENT",
                 eventId.toStdString(), pet::kEventIdMinSegments);
        return pet::PluginError::InvalidArgument;
    }
    // Merge rule: an existing (eventId, listener) pair is UPDATED in
    // place (priority + bookkeeping), never duplicated — the same
    // idempotence discipline as MonitorApiImpl::subscribe. The re-sort
    // keeps the priority ordering honest after a priority CHANGE.
    QVector<Entry>& entries = m_subscriptions[eventId];
    for (Entry& e : entries) {
        if (e.listener == listener) {
            e.priority = priority;
            e.publisherId = publisherId;
            std::stable_sort(entries.begin(), entries.end(),
                             [](const Entry& a, const Entry& b) {
                                 return static_cast<unsigned>(a.priority)
                                     < static_cast<unsigned>(b.priority);
                             });
            return pet::PluginError::Ok;
        }
    }
    Entry entry;
    entry.listener = listener;
    entry.priority = priority;
    entry.publisherId = publisherId;
    entries.append(entry);
    // Stable sort by priority VALUE ascending (Highest = 0 first);
    // stability preserves same-priority FIFO (append order).
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) {
                         return static_cast<unsigned>(a.priority)
                             < static_cast<unsigned>(b.priority);
                     });
    return pet::PluginError::Ok;
}

void EventBus::unsubscribe(const QString& eventId, pet::IEventListener* listener)
{
    const auto it = m_subscriptions.find(eventId);
    if (it == m_subscriptions.end())
        return; // unknown pair — legal no-op
    for (int i = it->size() - 1; i >= 0; --i) {
        if (it->at(i).listener == listener)
            it->remove(i);
    }
    if (it->isEmpty())
        m_subscriptions.erase(it);
}

pet::PluginError EventBus::publish(pet::Event& event)
{
    if (!isValidEventId(event.eventId)) {
        LOG_WARN("[event-bus] publish with illegal event id \"{}\" — "
                 "ERR_INVALID_ARGUMENT, zero dispatch",
                 event.eventId.toStdString());
        return pet::PluginError::InvalidArgument;
    }
    // Stamp (the emitter's timestamp never survives; publisherId is
    // forwarded verbatim — the proxy already stamped its own).
    event.timestampMs = QDateTime::currentMSecsSinceEpoch();

    const auto it = m_subscriptions.constFind(event.eventId);
    if (it == m_subscriptions.constEnd() || it->isEmpty())
        return pet::PluginError::Ok; // no subscribers: stamped, delivered

    if (t_publishDepth >= kEventMaxPublishDepth) {
        LOG_WARN("[event-bus] publish(\"{}\") at re-entrancy depth {} "
                 "(cap {}) — ERR_BUSY, zero dispatch",
                 event.eventId.toStdString(), t_publishDepth,
                 kEventMaxPublishDepth);
        return pet::PluginError::Busy;
    }

    // Snapshot copy: a listener may subscribe/unsubscribe (itself or
    // others) inside onEvent without invalidating this iteration — every
    // listener still in the snapshot receives exactly one call.
    const QVector<Entry> snapshot = it.value();

    ++t_publishDepth;
    for (const Entry& e : snapshot) {
        if (e.listener == nullptr)
            continue;
        try {
            e.listener->onEvent(event);
        } catch (...) {
            // §A.1 isolation: a throwing listener must not poison the
            // fanout (same discipline as InstanceApiImpl's observer
            // loops). Log and continue.
            LOG_ERROR("[event-bus] listener threw in onEvent() for "
                      "\"{}\" — isolated, fanout continues",
                      event.eventId.toStdString());
        }
        if (!event.cancellable)
            event.cancelled = false; // listener writes are void here
    }
    --t_publishDepth;
    return pet::PluginError::Ok;
}

int EventBus::unsubscribeByPublisher(const QString& publisherId)
{
    int removed = 0;
    auto channelIt = m_subscriptions.begin();
    while (channelIt != m_subscriptions.end()) {
        QVector<Entry>& entries = channelIt.value();
        for (int i = entries.size() - 1; i >= 0; --i) {
            if (entries.at(i).publisherId == publisherId) {
                entries.remove(i);
                ++removed;
            }
        }
        if (entries.isEmpty())
            channelIt = m_subscriptions.erase(channelIt);
        else
            ++channelIt;
    }
    return removed;
}

int EventBus::subscriptionCount() const
{
    int total = 0;
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it)
        total += it->size();
    return total;
}

} // namespace core
