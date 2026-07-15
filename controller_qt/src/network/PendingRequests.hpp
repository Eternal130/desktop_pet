#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QTimer>
#include <optional>

#include "network/Envelope.hpp"

// Pending-request manager (T10) — tracks outbound commands awaiting responses.
//
// Two interaction modes per interface.md §7.2:
//   - Request-Response: controller sends command(id=X), waits for response
//     with id=X, 10-second timeout. PendingRequests owns the table that maps
//     id → in-flight expectation and completes it when the matching response
//     arrives (via handleResponse, wired from MessageDispatcher's response
//     handler) or when the per-entry QTimer fires.
//
// Lifecycle of a single expectation:
//   1. expectResponse("X")          → installs a single-shot QTimer + entry.
//   2a. handleResponse(env id="X")  → resolves with env.success/error fields,
//                                     timer cancelled.
//   2b. timer fires (10s)           → resolves with timedOut=true.
//   3.  resolved(id, result) emitted exactly once per expectation.
//
// Re-registration of an already-pending id replaces the old expectation: the
// old entry's timer is cancelled and the old expectation is resolved with
// timedOut=true (so the caller observing the resolved signal learns it was
// superseded, not silently dropped).
//
// Threading: completions fire on the thread owning this object (the main
// thread). handleResponse() marshals itself onto that thread via
// QMetaObject::invokeMethod(Qt::AutoConnection) so it is safe to call from the
// WebSocket I/O thread. expectResponse() and the QTimer timeout must run on
// the owning thread (QTimer belongs to the thread that starts it).
struct PendingResult {
    bool timedOut = false;
    bool success = false;
    int errorCode = 0;
    QString errorMessage;
};

class PendingRequests : public QObject {
    Q_OBJECT
public:
    explicit PendingRequests(QObject* parent = nullptr);
    ~PendingRequests();

    // Register expectation for a response with the given id. Returns
    // immediately; connect to resolved(id, result) to observe completion.
    // timeoutMs < 0 (the default) uses m_defaultTimeoutMs (10000ms / whatever
    // setDefaultTimeoutMs installed); any positive value is used verbatim.
    // If the id is already pending, the old expectation is replaced (its timer
    // is cancelled and it is resolved with timedOut=true).
    void expectResponse(const QString& id, int timeoutMs = -1);

    // Called by MessageDispatcher when a type=="response" Envelope arrives.
    // If env.id matches a pending request → resolve it with env.success /
    // errorCode / errorMessage, cancel its timer. If env.id is unknown →
    // LOG_WARN + drop (never throws).
    void handleResponse(const Envelope& response);

    // Number of currently-pending requests (for testing / diagnostics).
    int pendingCount() const;

    // Test hook: set the default timeout used when expectResponse is called
    // with timeoutMs < 0. Production default is 10000ms (interface.md §7.2).
    void setDefaultTimeoutMs(int ms);

signals:
    // Emitted when a pending request completes — either by a matching
    // response (timedOut=false, fields populated) or by timeout
    // (timedOut=true, success=false). Emitted on the owning thread.
    void resolved(const QString& id, const PendingResult& result);

private:
    struct PendingEntry {
        QString id;
        QTimer* timer = nullptr;
    };
    QMap<QString, PendingEntry> m_entries;
    int m_defaultTimeoutMs = 10000;

    // Runs on the owning thread (marshaled from handleResponse). Looks up the
    // id; on miss logs + drops, on hit calls resolveEntry with the response
    // fields.
    void resolveFromResponse(const Envelope& env);

    // Removes the entry for id (if any), cancels + deletes its timer, emits
    // resolved(id, result). Idempotent: a no-op if the id is no longer pending
    // (guards the response-vs-late-timeout race).
    void resolveEntry(const QString& id, const PendingResult& result);
};
