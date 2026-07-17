#include "core/RestartController.hpp"

#include <algorithm>

#include <QTimer>

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

// RestartController implementation. See the .hpp for the full design notes.
//
// The QTimer::singleShot(receiver, lambda) form is used instead of a member
// QTimer: each retry arms a fresh one-shot timer parented to `this`, which Qt
// auto-destroys after it fires. If the controller (and its owning
// InstanceSession) is destroyed while a timer is pending, Qt cancels the
// pending timer safely — no use-after-free, no dangling lambda invocation.
//
// The schedule is non-blocking: scheduleRestart() returns immediately after
// arming the timer. The retry() signal fires later on the Qt main thread's
// event loop. This is critical because scheduleRestart is called from
// InstanceSession::onProcessExited, which itself runs inside ProcessManager's
// QProcess::finished signal — the QProcess is mid-teardown and must not be
// re-started synchronously from its own finished handler.

RestartController::RestartController(QObject* parent)
    : QObject(parent)
{
}

RestartController::~RestartController() = default;

void RestartController::setBackoffForTest(const QVector<int>& ms)
{
    // The test fast-ladder. Production code never calls this; tests inject
    // e.g. {10, 20, 30, 40, 50} so the full 5-rung sequence completes in
    // ~150ms instead of the real ~60s. Stored by value; the test owns the
    // controller for the slot's scope.
    m_backoffOverride = ms;
    LOG_DEBUG("RestartController: backoff overridden for test (size={})",
              ms.size());
}

void RestartController::scheduleRestart()
{
    // Ceiling reached → give up synchronously. InstanceSession wires this to
    // setStatus("error") + emit startFailed("max restart attempts reached").
    // Mirrors Java MainWindowController line 1524-1528.
    if (m_attempts >= kMaxAttempts) {
        LOG_WARN("RestartController: gave up after {} attempts", m_attempts);
        emit gaveUp();
        return;
    }

    // Compute the delay for the CURRENT attempt index (BEFORE the increment).
    // Index 0 = first retry = BACKOFF_MS[0] = 2000ms in production. Attempts
    // past the end of the ladder clamp to the last (cap) entry.
    const int delayMs = delayForCurrentAttempt();

    LOG_INFO("RestartController: scheduling retry #{} in {}ms",
             m_attempts + 1, delayMs);

    // Arm the one-shot. The lambda captures `this` to emit retry(); Qt
    // guarantees the lambda is NOT invoked if `this` is destroyed before the
    // timer fires (singleShot's receiver overload disconnects on destruction).
    // The increment happens HERE (before the timer fires), matching Java's
    // `restartAttempts.put(id, attempts + 1)` before the schedule() call —
    // so a crash during the backoff window sees the incremented count on the
    // next scheduleRestart entry, never double-counting.
    ++m_attempts;

    QTimer::singleShot(delayMs, this, [this]() {
        LOG_INFO("RestartController: firing retry (attempts={})", m_attempts);
        emit retry();
    });
}

void RestartController::resetOnConnect()
{
    // A successful WS reconnect means the renderer is healthy. Reset the
    // ladder so the next crash starts fresh from BACKOFF_MS[0]. Called by
    // InstanceSession::onConnectionStateChanged when state == Connected.
    if (m_attempts != 0) {
        LOG_INFO("RestartController: resetting attempts {} → 0 on connect",
                 m_attempts);
    }
    m_attempts = 0;
}

int RestartController::delayForCurrentAttempt() const
{
    // Test override wins when set. Otherwise use the constexpr production
    // ladder. Bounds via std::min so an attempt count past the last index
    // clamps to the cap (matches Java's Math.min(attempts, length-1)).
    const int idx = std::min(m_attempts, kMaxAttempts - 1);
    if (!m_backoffOverride.isEmpty()) {
        return m_backoffOverride.value(idx, m_backoffOverride.last());
    }
    return kBackoffMs[idx];
}
