#pragma once

#include <QMap>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

// Scheduler (Phase 5 todo 8) — QTimer-based idle motion scheduler, driven by
// a SINGLE QTimer on the Qt main thread (decision D3 — no QThread, single Qt
// main-thread event loop). Every timeout is delivered by the Qt event loop;
// the scheduler re-arms a single-shot QTimer after each tick, replicating a
// fixed-rate cadence (architecture-blueprint.md §8.3).
//
// API:
//   start(intervalMs, idleMotionGroups, onTrigger[, group])
//       Store groups + callback + interval; begin ticking. The group passed
//       to onTrigger is the configured group (default "Idle", D13); index is
//       randomized 0..(count-1) over motionGroups[group] via
//       QRandomGenerator::global()->bounded(count).
//   pause()      flag-only — the timer KEEPS TICKING but the callback early-
//                returns while m_paused is true (distinct from shutdown()).
//   resume()     clear the pause flag; triggers resume at the normal cadence
//                (the pending re-arm fires as scheduled, NOT immediately).
//   triggerNow() fire the callback IMMEDIATELY (ignoring pause), clear
//                m_paused, and restart the timer so the next tick is
//                intervalMs later. DISTINCT from resume — triggerNow always
//                fires + resets the cadence; resume only clears the flag.
//   updateInterval(ms)  change the interval; the next tick uses the new value.
//   setIdleMotions(groups)  replace the groups map (e.g. new model loaded).
//   shutdown()   stop the timer + clear state. No further triggers.
//
// The onTrigger callback receives (group, index). All exceptions thrown inside
// onTrigger are caught, logged via LOG_ERROR, and swallowed — handler
// isolation so the scheduler survives a bad callback (mirrors the Java
// reference's try/catch around onTrigger.accept). Exceptions never propagate
// to the QTimer event loop.
//
// Standalone for todo 8 (tested independently via SchedulerTest). Todo 10
// owns a Scheduler inside InstanceSession and starts it on model_loaded with
// the parsed motionGroups; the onTrigger callback there builds a play_motion
// command with priority=1 (interface.md §B.1 — Idle priority).
class Scheduler : public QObject {
    Q_OBJECT

public:
    // Default motion group cycled by the scheduler (D13 — configurable via
    // start()'s group param). Hiyori has Idle=9; the index randomizes over
    // that count. Kept as a static so callers can reference the same literal.
    static const QString kDefaultGroup;

    // Callback shape: receives the group name + a randomized index into
    // motionGroups[group]. Invoked ON the Qt main thread (the QTimer's owner
    // thread). Exceptions thrown from the callback are swallowed + logged.
    using OnTrigger = std::function<void(const QString& group, int index)>;

    explicit Scheduler(QObject* parent = nullptr);
    ~Scheduler() override;

public slots:
    // Begin ticking. intervalMs must be >= 0 (0 = fire as fast as the event
    // loop allows; negative values are clamped to 0 with a WARN). Stores a
    // copy of groups + onTrigger, sets m_group, clears m_paused, and arms
    // the single-shot QTimer for the first tick (intervalMs later).
    //
    // Calling start() on an already-running scheduler restarts it with the
    // new parameters (cancels the pending re-arm, re-arms with the new
    // interval). Mirrors Java's start() which cancelCurrentTask() +
    // scheduleTask().
    void start(int intervalMs, const QMap<QString, int>& idleMotionGroups,
               OnTrigger onTrigger,
               const QString& group = kDefaultGroup);

    // Flag-only pause: the timer KEEPS TICKING (onTimeout keeps re-arming)
    // but the callback early-returns while m_paused is true. Distinct from
    // shutdown() which stops the timer entirely. Blueprint §8.3: "仅切换暂停
    // 标志，调度器继续运行但不触发回调".
    void pause();

    // Clear the pause flag. Triggers resume at the normal cadence (the pending
    // re-arm fires as scheduled — NOT immediately). The counterpart of pause().
    void resume();

    // Fire the callback IMMEDIATELY (ignoring pause), clear m_paused, and
    // restart the timer so the next tick is intervalMs later. Unlike resume(),
    // triggerNow ALWAYS fires + resets the cadence. Blueprint §8.3: "立即触发
    // 一次回调 + 重置周期定时器。用于非闲时动作结束后的快速衔接".
    //
    // No-op if the configured group is absent or has count 0 (the fireOnce
    // step returns early; the re-arm still happens so cadence is reset for
    // the next setIdleMotions arrival). No-op if not running.
    void triggerNow();

    // Change the interval; the next tick uses the new value. Restarts the
    // pending single-shot so the new interval takes effect immediately (not
    // at the next scheduled tick). No-op if not running.
    void updateInterval(int ms);

    // Replace the motion-groups map (e.g. when a new model loads with a
    // different Idle count). Does not disturb the running timer — the next
    // fireOnce reads the fresh count. Mirrors Java's setIdleMotions().
    void setIdleMotions(const QMap<QString, int>& groups);

    // Stop the timer + clear runtime state (callback, groups). No further
    // triggers fire after shutdown. isRunning() returns false. Idempotent —
    // safe to call on an already-stopped scheduler.
    void shutdown();

public:
    // Observers (for tests / QML introspection). Inline trivial accessors.
    bool isRunning() const { return m_running; }
    bool isPaused() const  { return m_paused; }
    int  intervalMs() const { return m_intervalMs; }
    QString group() const  { return m_group; }

signals:
    // Emitted for every successful callback invocation (after the try/catch
    // swallows any exception). Carries the same (group, index) pair passed to
    // onTrigger. Useful for QSignalSpy-based tests + a future command-log
    // panel. NOT emitted when the group is absent/empty (fireOnce early-
    // returns) or when paused.
    void triggered(const QString& group, int index);

private:
    // QTimer::timeout handler. If running + !paused + group count > 0, call
    // fireOnce(). Then re-check m_running and re-arm the single-shot so the
    // cadence continues (pause = flag-only — the timer keeps the beat so
    // resume picks up at the right point).
    void onTimeout();

    // Fire the callback once with a randomized index over
    // motionGroups[m_group]. Catches std::exception + ... (logs via
    // LOG_ERROR), then emits triggered(). No-op when the group is absent or
    // has count 0 — matches Java's `if (motions.isEmpty()) return`.
    void fireOnce();

    // (Re-)arm the single-shot QTimer for m_intervalMs. start() on a
    // single-shot timer cancels any pending timeout and arms a fresh one.
    void rearm();

    // Single-shot QTimer parented to this Scheduler (Qt deletes it). The
    // recursive re-arm pattern (onTimeout → rearm) gives Java's
    // scheduleAtFixedRate semantics: a fresh interval begins after each
    // callback completes.
    QTimer* m_timer = nullptr;

    QMap<QString, int> m_motionGroups;   // group name → motion count
    QString m_group = kDefaultGroup;     // which group's count to randomize over
    OnTrigger m_onTrigger;               // the per-tick callback (nullable)

    int  m_intervalMs = 0;
    bool m_paused = false;
    bool m_running = false;
};
