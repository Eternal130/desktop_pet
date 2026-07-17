#include "core/Scheduler.hpp"

#include <QRandomGenerator>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <utility>

#include "logging/Logging.hpp"

const QString Scheduler::kDefaultGroup = QStringLiteral("Idle");

Scheduler::Scheduler(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))  // parented → Qt deletes it; never double-free
{
    // Single-shot + recursive re-arm (D3): each timeout fires the callback,
    // then rearm() starts a fresh single-shot. This replicates Java's
    // scheduleAtFixedRate — a new interval begins after each tick.
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Scheduler::onTimeout);
}

Scheduler::~Scheduler() = default;

void Scheduler::start(int intervalMs, const QMap<QString, int>& idleMotionGroups,
                      OnTrigger onTrigger, const QString& group)
{
    if (intervalMs < 0) {
        LOG_WARN("Scheduler::start: intervalMs={} < 0; clamping to 0", intervalMs);
        intervalMs = 0;
    }
    m_intervalMs   = intervalMs;
    m_motionGroups = idleMotionGroups;
    m_onTrigger    = std::move(onTrigger);
    m_group        = group;
    m_running      = true;
    m_paused       = false;
    // Cancels any pending re-arm (Java's cancelCurrentTask) + arms the first
    // tick intervalMs out.
    rearm();
}

void Scheduler::pause()
{
    // Flag-only — the timer keeps ticking; onTimeout early-returns while
    // m_paused is true. Distinct from shutdown() which stops the timer.
    m_paused = true;
}

void Scheduler::resume()
{
    // Clear the flag only. The pending re-arm fires as scheduled (NOT
    // immediately) — that is the contract that distinguishes resume() from
    // triggerNow().
    m_paused = false;
}

void Scheduler::triggerNow()
{
    if (!m_running) {
        return;
    }
    // Java's triggerNow clears paused state before firing — so a triggerNow
    // while paused both delivers the immediate fire AND un-pauses for
    // subsequent ticks.
    m_paused = false;
    fireOnce();  // ignores pause — always fires (when group present)
    rearm();     // reset cadence: next tick is intervalMs later, not sooner
}

void Scheduler::updateInterval(int ms)
{
    if (ms < 0) {
        LOG_WARN("Scheduler::updateInterval: ms={} < 0; clamping to 0", ms);
        ms = 0;
    }
    m_intervalMs = ms;
    if (m_running) {
        // Restart so the new interval takes effect immediately (cancels the
        // pending re-arm, arms a fresh one for ms). Mirrors Java's
        // cancelCurrentTask + scheduleTask inside updateInterval.
        rearm();
    }
}

void Scheduler::setIdleMotions(const QMap<QString, int>& groups)
{
    // Replace the groups map without touching the timer — the next fireOnce
    // reads the fresh count. Used when a new model loads with a different
    // Idle count (todo 10 wires this from model_loaded).
    m_motionGroups = groups;
}

void Scheduler::shutdown()
{
    m_running = false;
    m_paused  = true;
    m_timer->stop();
    // Drop the callback + groups so a stale closure (e.g. capturing an
    // InstanceSession that's being destroyed) can never fire after shutdown.
    m_onTrigger = nullptr;
    m_motionGroups.clear();
}

void Scheduler::onTimeout()
{
    if (!m_running) {
        // shutdown() raced with a pending timeout delivery — drop it. The
        // timer was stop()ed, but a timeout already in the event queue can
        // still be dispatched.
        return;
    }
    if (!m_paused) {
        fireOnce();
    }
    // Re-check m_running: the callback may have invoked shutdown() (e.g. a
    // one-shot schedule that self-terminates). Only re-arm if still running.
    // Pause does NOT suppress the re-arm — the timer keeps the beat so
    // resume() picks up at the next scheduled tick (blueprint §8.3 "调度器继
    // 续运行但不触发回调").
    if (m_running) {
        rearm();
    }
}

void Scheduler::fireOnce()
{
    const auto it = m_motionGroups.constFind(m_group);
    if (it == m_motionGroups.constEnd() || *it <= 0) {
        // Group absent or zero-count: skip this tick (no trigger). Matches
        // Java's `if (motions.isEmpty()) return;`. NOT a crash, NOT a log —
        // a routine skip when the model has no Idle motions.
        return;
    }
    const int count = *it;
    const int index = QRandomGenerator::global()->bounded(count);  // 0..count-1

    // Handler isolation: a throwing callback must NEVER propagate to the
    // QTimer event loop (it would abort the app). Catch std::exception +
    // catch-all, log via LOG_ERROR, swallow. The scheduler survives + the
    // next tick still fires.
    try {
        if (m_onTrigger) {
            m_onTrigger(m_group, index);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Scheduler: onTrigger threw (group='{}', index={}): {}",
                  m_group.toStdString(), index, e.what());
    } catch (...) {
        LOG_ERROR("Scheduler: onTrigger threw non-std::exception "
                  "(group='{}', index={})", m_group.toStdString(), index);
    }

    // Emit AFTER the try/catch so tests (QSignalSpy) + a future log panel see
    // the trigger even when the callback threw. Mirrors Java's structure
    // where the scheduleAtFixedRate task body always completes.
    emit triggered(m_group, index);
}

void Scheduler::rearm()
{
    // QTimer::start(int) on a single-shot timer cancels any pending timeout
    // and arms a fresh one for the given interval. This is the recursive
    // re-arm that gives Java's scheduleAtFixedRate cadence.
    m_timer->start(m_intervalMs);
}
