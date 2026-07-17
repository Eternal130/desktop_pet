// SchedulerTest (Phase 5 todo 8) — QTimer-based idle motion scheduler.
//
// Eight slots covering the full Scheduler API + the blueprint §8.3 contract:
//
//   testCadence          — start(50ms, {Idle:2}) → ≥3 triggers in 200ms, each
//                          index ∈ {0,1}, group == "Idle".
//   testPause            — pause() → 0 new triggers in 200ms (flag-only).
//   testResume           — pause then resume → triggers resume at cadence.
//   testTriggerNow       — triggerNow() fires immediately despite a long
//                          interval (10s) + despite pause; cadence reset.
//   testUpdateInterval   — start(10s) → 0 triggers; updateInterval(50) → ≥3
//                          in 200ms (new interval takes effect immediately).
//   testShutdown         — shutdown() → no further triggers; isRunning==false.
//   testEmptyIdleMotions — start with empty groups → 0 triggers, no crash.
//                          Also covers group-present-but-count-0.
//   testExceptionSurvives — onTrigger throws on first call → swallowed +
//                          logged; scheduler survives + fires again.
//
// QTEST_MAIN (NOT APPLESS): QTimer requires the QCoreApplication event loop,
// pumped by QTest::qWait. The Scheduler's QTimer is parented to the Scheduler
// (stack-local in each slot) so it is destroyed when the slot returns — no
// cross-slot leakage. Each slot calls shutdown() before its assertions so the
// timer is stopped before the QCOMPARE/QVERIFY messages (avoids a trigger
// firing mid-assertion).
//
// Timing philosophy: intervalMs=50 + qWait(200) yields ~3-4 triggers (50, 100,
// 150, 200ms boundaries). Assertions use `>= 3` (never exact counts) so the
// tests are robust to event-loop scheduling jitter without being tautological.

#include <QMap>
#include <QObject>
#include <QString>
#include <QTest>

#include <stdexcept>

#include "core/Scheduler.hpp"

class SchedulerTest : public QObject {
    Q_OBJECT

private slots:
    void testCadence();
    void testPause();
    void testResume();
    void testTriggerNow();
    void testUpdateInterval();
    void testShutdown();
    void testEmptyIdleMotions();
    void testExceptionSurvives();
};

// ────────────────────────────────────────────────────────────────────────────
// Cadence: the timer ticks at the configured interval, the callback receives
// (group="Idle", index ∈ [0, count)), and the count advances over a 200ms
// window. ≥3 triggers expected at 50ms cadence.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testCadence()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    bool indexInRange = true;
    QString lastGroup;
    sched.start(50, groups, [&](const QString& group, int index) {
        lastGroup = group;
        if (index < 0 || index >= 2) {
            indexInRange = false;
        }
        ++count;
    });

    QTest::qWait(200);
    sched.shutdown();  // stop before assertions to avoid mid-check triggers

    QVERIFY2(sched.isRunning() == false, "shutdown() must clear isRunning");
    QVERIFY2(count >= 3,
             qPrintable(QStringLiteral("expected >=3 triggers at 50ms cadence "
                                       "in 200ms, got %1").arg(count)));
    QVERIFY2(indexInRange, "an out-of-range index was emitted");
    QCOMPARE(lastGroup, QStringLiteral("Idle"));
}

// ────────────────────────────────────────────────────────────────────────────
// Pause is flag-only: the timer keeps re-arming but the callback does not fire.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testPause()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    sched.start(50, groups, [&](const QString&, int) { ++count; });

    QTest::qWait(120);  // ~2 triggers before pause
    const int beforePause = count;
    QVERIFY2(beforePause >= 1,
             qPrintable(QStringLiteral("expected >=1 trigger before pause, "
                                       "got %1").arg(beforePause)));

    sched.pause();
    QVERIFY(sched.isPaused());
    QTest::qWait(200);
    QCOMPARE(count, beforePause);  // zero new triggers while paused

    sched.shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// resume() clears the flag; triggers resume at the normal cadence.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testResume()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    sched.start(50, groups, [&](const QString&, int) { ++count; });

    sched.pause();
    QTest::qWait(150);
    QCOMPARE(count, 0);  // paused → no triggers
    QVERIFY(sched.isPaused());

    sched.resume();
    QVERIFY(!sched.isPaused());
    QTest::qWait(200);
    QVERIFY2(count >= 3,
             qPrintable(QStringLiteral("expected >=3 triggers after resume, "
                                       "got %1").arg(count)));

    sched.shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// triggerNow fires IMMEDIATELY despite a long interval + despite pause, then
// resets the cadence (next tick is intervalMs later → none in a short window).
// Uses a 10s interval so the timer-driven path NEVER fires in the test window;
// only triggerNow can produce a trigger.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testTriggerNow()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 1);

    int count = 0;
    sched.start(10000, groups, [&](const QString&, int) { ++count; });

    // Long interval → no triggers in 150ms.
    QTest::qWait(150);
    QCOMPARE(count, 0);

    // triggerNow fires immediately despite the 10s interval.
    sched.triggerNow();
    QCOMPARE(count, 1);

    // Cadence reset: next tick is 10s away → none in 200ms.
    QTest::qWait(200);
    QCOMPARE(count, 1);

    // Also: triggerNow fires even when paused (it clears the pause flag).
    sched.pause();
    QTest::qWait(150);
    QCOMPARE(count, 1);  // still paused + long interval → no trigger
    sched.triggerNow();
    QCOMPARE(count, 2);  // fires despite pause
    QVERIFY(!sched.isPaused());  // triggerNow cleared the flag

    sched.shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// updateInterval takes effect IMMEDIATELY (re-arm, not at next scheduled tick).
// Start with 10s → 0 triggers; shorten to 50ms → ≥3 in 200ms.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testUpdateInterval()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    sched.start(10000, groups, [&](const QString&, int) { ++count; });

    QTest::qWait(150);
    QCOMPARE(count, 0);  // long interval → no triggers
    QCOMPARE(sched.intervalMs(), 10000);

    sched.updateInterval(50);
    QCOMPARE(sched.intervalMs(), 50);
    QTest::qWait(200);
    QVERIFY2(count >= 3,
             qPrintable(QStringLiteral("expected >=3 triggers after "
                                       "updateInterval(50), got %1").arg(count)));

    sched.shutdown();
}

// ────────────────────────────────────────────────────────────────────────────
// shutdown stops the timer + clears state; no further triggers fire.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testShutdown()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    sched.start(50, groups, [&](const QString&, int) { ++count; });

    QTest::qWait(120);
    const int before = count;
    QVERIFY2(before >= 1,
             qPrintable(QStringLiteral("expected >=1 trigger before shutdown, "
                                       "got %1").arg(before)));

    sched.shutdown();
    QVERIFY(!sched.isRunning());
    QTest::qWait(200);
    QCOMPARE(count, before);  // no further triggers after shutdown

    // Idempotent: shutdown on an already-stopped scheduler is a safe no-op.
    sched.shutdown();
    QVERIFY(!sched.isRunning());
}

// ────────────────────────────────────────────────────────────────────────────
// Empty idle motions: the callback never fires, no crash. Covers both the
// missing-group case (empty map) and the present-but-zero-count case.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testEmptyIdleMotions()
{
    {
        Scheduler sched;
        QMap<QString, int> groups;  // empty — group "Idle" absent

        int count = 0;
        sched.start(50, groups, [&](const QString&, int) { ++count; });

        QTest::qWait(200);
        QCOMPARE(count, 0);  // group absent → fireOnce early-returns
        QVERIFY(sched.isRunning());  // timer still ticking, just not firing

        sched.shutdown();
    }

    {
        // Group present but count 0 — same skip behavior.
        Scheduler sched;
        QMap<QString, int> groups;
        groups.insert(QStringLiteral("Idle"), 0);

        int count = 0;
        sched.start(50, groups, [&](const QString&, int) { ++count; });

        QTest::qWait(200);
        QCOMPARE(count, 0);

        sched.shutdown();
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Exception isolation: an onTrigger that throws must NOT kill the scheduler.
// The exception is swallowed + logged; the next tick still fires. This is the
// handler-isolation contract — one bad callback never takes down idle cycling.
// ────────────────────────────────────────────────────────────────────────────
void SchedulerTest::testExceptionSurvives()
{
    Scheduler sched;
    QMap<QString, int> groups;
    groups.insert(QStringLiteral("Idle"), 2);

    int count = 0;
    sched.start(50, groups, [&](const QString&, int) {
        ++count;
        if (count == 1) {
            throw std::runtime_error("boom");
        }
    });

    QTest::qWait(200);
    // count >= 2 proves the scheduler survived the first throw AND fired again.
    QVERIFY2(count >= 2,
             qPrintable(QStringLiteral("scheduler did not survive the "
                                       "exception (count=%1, expected >=2)")
                        .arg(count)));

    sched.shutdown();
}

QTEST_MAIN(SchedulerTest)

#include "SchedulerTest.moc"
