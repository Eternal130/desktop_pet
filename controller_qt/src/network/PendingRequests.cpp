#include "network/PendingRequests.hpp"

#include <QMetaObject>
#include <QTimer>

#include <spdlog/spdlog.h>

#include "logging/Logging.hpp"

PendingRequests::PendingRequests(QObject* parent)
    : QObject(parent) {}

PendingRequests::~PendingRequests() {
    // Cancel + delete every live timer. Entries still pending at destruction
    // are abandoned silently (no resolved signal) — destruction only happens
    // at app shutdown after the WS connection is torn down, so no in-flight
    // command still cares about its result.
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        QTimer* timer = it.value().timer;
        if (timer) {
            timer->stop();
            timer->disconnect();
            delete timer;
            it.value().timer = nullptr;
        }
    }
    m_entries.clear();
}

void PendingRequests::setDefaultTimeoutMs(int ms) {
    m_defaultTimeoutMs = ms;
}

int PendingRequests::pendingCount() const {
    return m_entries.size();
}

void PendingRequests::expectResponse(const QString& id, int timeoutMs) {
    // Replace semantics (header contract): if id is already pending, resolve
    // the OLD expectation with a timeout first so its observer learns it was
    // superseded — never silently dropped. The new expectation is then
    // installed fresh.
    if (m_entries.contains(id)) {
        resolveEntry(id, PendingResult{true, false, 0, QString()});
    }

    const int effectiveMs = (timeoutMs < 0) ? m_defaultTimeoutMs : timeoutMs;

    // No parent: this map owns the timer's lifetime explicitly (resolveEntry
    // deletes it). A QTimer belongs to (and fires on) the thread that creates
    // it — expectResponse runs on the owning thread, so the timeout callback
    // fires here too.
    QTimer* timer = new QTimer();
    timer->setSingleShot(true);
    // id captured by value: even if other entries come and go, this callback
    // always resolves the right one.
    connect(timer, &QTimer::timeout, this, [this, id]() {
        resolveEntry(id, PendingResult{true, false, 0, QString()});
    });
    timer->start(effectiveMs);

    m_entries.insert(id, PendingEntry{id, timer});
    LOG_DEBUG("pending request registered (id='{}', timeoutMs={})",
              id.toStdString(), effectiveMs);
}

void PendingRequests::handleResponse(const Envelope& response) {
    // Marshal onto the thread owning `this` so resolveEntry + the resolved
    // signal fire on the main thread (interface.md §7.2 completion path, and
    // the receiver contract every caller assumes). AutoConnection = direct
    // call when same thread (tests), queued when cross-thread (production:
    // WS I/O thread → main thread). Passing `this` as the context also gives
    // Qt a lifetime guard — if `this` is destroyed before a queued functor
    // runs, the functor is discarded.
    //
    // The functor-based invokeMethod overload copies captures into the posted
    // event; it does NOT require Q_DECLARE_METATYPE / qRegisterMetaType for the
    // captured Envelope (unlike the string-based Q_ARG form).
    const Envelope env = response;
    QMetaObject::invokeMethod(this, [this, env]() { resolveFromResponse(env); },
                              Qt::AutoConnection);
}

void PendingRequests::resolveFromResponse(const Envelope& env) {
    auto it = m_entries.find(env.id);
    if (it == m_entries.end()) {
        // Unknown id: a response arrived with no matching pending request.
        // Log + drop — never throw (the WS message pump must keep routing).
        LOG_WARN("response for unknown id '{}', dropping (action='{}')",
                 env.id.toStdString(), env.action.toStdString());
        return;
    }
    resolveEntry(env.id, PendingResult{
        /*timedOut=*/false,
        /*success=*/env.success,
        /*errorCode=*/env.errorCode,
        /*errorMessage=*/env.errorMessage,
    });
}

void PendingRequests::resolveEntry(const QString& id, const PendingResult& result) {
    auto it = m_entries.find(id);
    if (it == m_entries.end()) {
        // Already resolved. This guards the race where a response and a late
        // timeout both target the same id: the second arrival finds no entry
        // and becomes a no-op, so resolved() fires exactly once.
        return;
    }

    QTimer* timer = it.value().timer;
    m_entries.erase(it);

    if (timer) {
        timer->stop();
        timer->disconnect(); // drop the timeout→lambda connection before delete
        delete timer;
    }

    LOG_DEBUG("pending request resolved (id='{}', timedOut={}, success={}, errorCode={})",
              id.toStdString(), result.timedOut, result.success, result.errorCode);
    emit resolved(id, result);
}
