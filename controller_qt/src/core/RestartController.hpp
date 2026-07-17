#pragma once

#include <QObject>
#include <QVector>

// RestartController (Phase 5 Wave 7, todo 12) — crash-recovery backoff
// scheduler. A QObject owning the per-instance restart-attempt count and the
// exponential-backoff ladder. Port of Java's
// `controller/.../ui/MainWindowController.java::scheduleRestart` (lines
// 1522-1542), translated from `ScheduledExecutorService.schedule(...)` to
// `QTimer::singleShot(ms, this, lambda)` (decision D3 — single Qt main-thread
// event loop, no extra thread).
//
// Ownership decision (todo 12 plan, "DECISION: per-instance"): each
// InstanceSession owns its OWN RestartController (same pattern as Scheduler /
// InteractionHandler / HitAreaCacheManager). The API therefore carries NO
// instanceId parameter — the bound instance is implicit. This is cleaner than
// a shared RestartController keyed by QMap<int,int> and matches the
// InstanceSession architecture (one orchestrator tuple per pet).
//
// Constants (architecture-blueprint.md §8.2 crash-recovery spec):
//   MAX_ATTEMPTS = 5
//   BACKOFF_MS   = [2000, 4000, 8000, 16000, 30000]   (≈2ⁿ seconds, capped 30s)
//
// API:
//   scheduleRestart()    — entry point from InstanceSession::onProcessExited
//                          (only when !m_manuallyStopping && crashed). If
//                          attempts >= MAX_ATTEMPTS → emit gaveUp() (the
//                          session flips to status="error"). Else arm a
//                          QTimer::singleShot(BACKOFF_MS[min(attempts, 4)])
//                          that emits retry() on timeout, then ++m_attempts.
//   resetOnConnect()     — InstanceSession calls this from
//                          onConnectionStateChanged when state == Connected.
//                          Resets m_attempts=0 so a SUCCESSFUL reconnect
//                          restarts the backoff ladder from the bottom.
//
// Signals:
//   retry()              — fired on the QTimer::singleShot timeout. Wired in
//                          InstanceSession to call session.start() again (the
//                          process is already in NotRunning state by then).
//   gaveUp()             — fired synchronously inside scheduleRestart() when
//                          the attempt ceiling is hit. Wired to setStatus
//                          ("error") + emit startFailed("max restart attempts
//                          reached").
//
// Test seam:
//   setBackoffForTest(ms) — replaces the production BACKOFF_MS ladder with a
//                           caller-supplied vector (typically [10,20,30,40,50]
//                           ms) so the test suite can exercise the full 5-rung
//                           ladder in well under a second. The production
//                           default ladder is restored only by destroying the
//                           controller (test scope), which is fine for the
//                           QTEST_MAIN slot-per-instance pattern.
//
// Thread safety: lives on the Qt main thread. QTimer::singleShot schedules
// on the owning thread's event loop. No GL/network calls happen here.
class RestartController : public QObject {
    Q_OBJECT

public:
    // Crash-recovery ceiling — after this many retry attempts, giveUp fires
    // and the session enters terminal error state (manual restart required).
    // architecture-blueprint.md §8.2 + Java MainWindowController line 89.
    static constexpr int kMaxAttempts = 5;

    // Exponential backoff ladder in milliseconds. Index 0 is the first retry
    // delay; the last entry is the cap. Indexed by min(m_attempts, size-1).
    // Java MainWindowController line 90: {2000, 4000, 8000, 16000, 30000}.
    static constexpr int kBackoffMs[kMaxAttempts] = {2000, 4000, 8000, 16000, 30000};

    explicit RestartController(QObject* parent = nullptr);
    ~RestartController() override;

    // Observer (for tests / QML introspection). The current attempt count
    // since the last successful connect (resetOnConnect clears it).
    int attempts() const { return m_attempts; }

    // Test seam: override the production BACKOFF_MS ladder with a faster one
    // so the test suite can assert the full 5-rung sequence in milliseconds.
    // The vector MUST have at least kMaxAttempts entries; extra entries are
    // ignored (indexing uses min(attempts, kMaxAttempts-1)).
    void setBackoffForTest(const QVector<int>& ms);

public slots:
    // Entry point from InstanceSession::onProcessExited when the renderer
    // crashed unexpectedly (!manuallyStopping). Arms the next backoff delay
    // and emits retry() on timeout, OR emits gaveUp() synchronously when the
    // attempt ceiling has been reached.
    void scheduleRestart();

    // Reset the attempt counter to 0. Called by InstanceSession when the WS
    // connection reaches the Connected state — a successful reconnect means
    // the renderer is healthy and the backoff ladder should start fresh on
    // the next crash.
    void resetOnConnect();

signals:
    // Fired on the QTimer::singleShot timeout (delay = BACKOFF_MS[min(
    // attempts, kMaxAttempts-1)]). Wire to InstanceSession::start (or a
    // helper that resets manuallyStopping first, then calls start()).
    void retry();

    // Fired synchronously inside scheduleRestart() when attempts >=
    // kMaxAttempts. Wire to InstanceSession::setStatus("error") + emit
    // startFailed("max restart attempts reached").
    void gaveUp();

private:
    // Resolve the delay for the CURRENT attempt count. Production: reads
    // kBackoffMs[min(m_attempts, kMaxAttempts-1)]. Test-overridden: reads
    // m_backoffOverride instead. Bounds-checked via std::min so an attempt
    // count past the end clamps to the last (cap) entry.
    int delayForCurrentAttempt() const;

    int             m_attempts = 0;
    // Empty by default → delayForCurrentAttempt uses kBackoffMs. When set by
    // setBackoffForTest, used instead (test fast-ladder).
    QVector<int>    m_backoffOverride;
};
