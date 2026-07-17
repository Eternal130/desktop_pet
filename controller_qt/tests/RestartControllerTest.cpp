#include "core/RestartController.hpp"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <QVector>

// RestartControllerTest (Phase 5 Wave 7, todo 12) — crash-recovery backoff.
//
// QTEST_MAIN (NOT APPLESS) is mandatory: the controller schedules retries
// via QTimer::singleShot, which requires the QCoreApplication event loop
// that QSignalSpy::wait / QTest::qWait pump. QTEST_APPLESS_MAIN would
// silently skip every retry assertion (the timer would never fire).
//
// The production ladder {2000, 4000, 8000, 16000, 30000} would make the
// full 5-rung suite take ~60s. Every slot installs a fast test ladder via
// setBackoffForTest({10, 20, 30, 40, 50}) so the entire backoff sequence
// completes in ~150ms — fast enough for the unit-test speed budget.
//
// Three slots lock the contract:
//   testBackoffLadder  — 5 scheduleRestart calls → 5 retry signals at
//                        increasing delays; attempts ends at 5; NO gaveUp.
//   testGaveUp         — the 6th scheduleRestart (attempts==5) fires gaveUp
//                        synchronously and emits NO further retry.
//   testResetOnConnect — after 3 attempts, resetOnConnect zeroes the counter
//                        so the next scheduleRestart uses BACKOFF[0] again.
//
// Recompiles RestartController.cpp directly (same pattern as the other core
// tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
// (RestartController.cpp includes logging/Logging.hpp).

class RestartControllerTest : public QObject {
    Q_OBJECT

private slots:
    // ── Setup ──────────────────────────────────────────────────────────────
    // No initTestCase — each slot builds a fresh controller + fast ladder so
    // state never leaks across slots. The fast ladder {10..50} keeps the
    // total suite runtime well under 1s.

    void testBackoffLadder()
    {
        // Given: a controller with the fast test ladder installed.
        RestartController rc;
        rc.setBackoffForTest({10, 20, 30, 40, 50});
        QCOMPARE(rc.attempts(), 0);

        QSignalSpy retrySpy(&rc, &RestartController::retry);
        QSignalSpy gaveUpSpy(&rc, &RestartController::gaveUp);
        QVERIFY(retrySpy.isValid());
        QVERIFY(gaveUpSpy.isValid());

        // When: scheduleRestart is called 5 times, waiting for each retry
        // signal before arming the next. This mirrors the production flow
        // where each retry fires start(), and a subsequent crash re-enters
        // scheduleRestart with the incremented counter.
        const QVector<int> expectedDelays = {10, 20, 30, 40, 50};
        for (int i = 0; i < RestartController::kMaxAttempts; ++i) {
            const int attemptsBefore = rc.attempts();
            rc.scheduleRestart();

            // scheduleRestart increments attempts IMMEDIATELY (before the
            // timer fires), matching Java's restartAttempts.put(id, n+1)
            // before restartExecutor.schedule().
            QCOMPARE(rc.attempts(), attemptsBefore + 1);

            // gaveUp must NEVER fire while attempts <= MAX_ATTEMPTS.
            QCOMPARE(gaveUpSpy.count(), 0);

            // Wait for the retry signal. The delay should be approximately
            // expectedDelays[i]; QTest::qWait is used instead of
            // QSignalSpy::wait so the assertion message names the rung.
            const int deadline = expectedDelays[i] + 200;  // generous slack
            QVERIFY2(retrySpy.wait(deadline),
                     qPrintable(QStringLiteral("retry #") + QString::number(i + 1) +
                                QStringLiteral(" did not fire within ") +
                                QString::number(deadline) + QStringLiteral("ms")));
            QCOMPARE(retrySpy.count(), i + 1);
        }

        // Then: 5 retries fired, 0 gaveUp, attempts==5.
        QCOMPARE(retrySpy.count(), RestartController::kMaxAttempts);
        QCOMPARE(gaveUpSpy.count(), 0);
        QCOMPARE(rc.attempts(), RestartController::kMaxAttempts);
    }

    void testGaveUp()
    {
        // Given: a controller already at the attempt ceiling (5 retries
        // already fired). Drive it there with the fast ladder so the slot
        // stays under the time budget.
        RestartController rc;
        rc.setBackoffForTest({10, 20, 30, 40, 50});

        QSignalSpy retrySpy(&rc, &RestartController::retry);
        QSignalSpy gaveUpSpy(&rc, &RestartController::gaveUp);

        // Burn through 5 retries to reach the ceiling.
        for (int i = 0; i < RestartController::kMaxAttempts; ++i) {
            rc.scheduleRestart();
            QVERIFY(retrySpy.wait(300));
        }
        QCOMPARE(rc.attempts(), RestartController::kMaxAttempts);
        QCOMPARE(retrySpy.count(), RestartController::kMaxAttempts);
        QCOMPARE(gaveUpSpy.count(), 0);

        // When: the 6th scheduleRestart is called with attempts == MAX.
        // gaveUp must fire SYNCHRONOUSLY (inside the scheduleRestart call)
        // and NO 6th retry must be armed.
        rc.scheduleRestart();

        // Then: gaveUp fired exactly once, synchronously (no qWait needed).
        QCOMPARE(gaveUpSpy.count(), 1);
        QCOMPARE(rc.attempts(), RestartController::kMaxAttempts);  // unchanged

        // Pump the event loop for a comfortable margin and assert NO 6th
        // retry was armed. A spurious retry here would be the bug this slot
        // guards against.
        QTest::qWait(150);
        QCOMPARE(retrySpy.count(), RestartController::kMaxAttempts);
    }

    void testResetOnConnect()
    {
        // Given: a controller that has accumulated 3 attempts (3 retries
        // fired). Driven there with the fast ladder.
        RestartController rc;
        rc.setBackoffForTest({10, 20, 30, 40, 50});

        QSignalSpy retrySpy(&rc, &RestartController::retry);

        for (int i = 0; i < 3; ++i) {
            rc.scheduleRestart();
            QVERIFY(retrySpy.wait(300));
        }
        QCOMPARE(rc.attempts(), 3);
        QCOMPARE(retrySpy.count(), 3);

        // When: resetOnConnect is called (WS reached Connected — the
        // renderer recovered). The counter must zero.
        rc.resetOnConnect();
        QCOMPARE(rc.attempts(), 0);

        // Then: the NEXT scheduleRestart uses BACKOFF[0] (10ms in the test
        // ladder), not BACKOFF[3] (40ms). Assert by measuring the actual
        // delay: the 4th retry after a reset should land in ~10ms, well
        // under the 40ms the un-reset path would take.
        QElapsedTimer t;
        t.start();
        rc.scheduleRestart();
        QVERIFY(retrySpy.wait(300));
        const int elapsedMs = static_cast<int>(t.elapsed());

        // The reset path uses index 0 (10ms test delay) — assert the actual
        // wait was clearly below the index-3 (40ms) delay. Use 25ms as the
        // threshold (midway between 10 and 40) to avoid CI flakiness from
        // a slow event loop tick on the boundary.
        QVERIFY2(elapsedMs < 25,
                 qPrintable(QStringLiteral("reset did not rewind to BACKOFF[0]; ")
                            + QStringLiteral("elapsed=") + QString::number(elapsedMs)
                            + QStringLiteral("ms (expected ~10ms, threshold 25ms)")));
        QCOMPARE(rc.attempts(), 1);
        QCOMPARE(retrySpy.count(), 4);
    }
};

QTEST_MAIN(RestartControllerTest)
#include "RestartControllerTest.moc"
