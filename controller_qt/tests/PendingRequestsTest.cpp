#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariant>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"

// PendingResult must be a metatype for QSignalSpy to store it in a QVariant so
// the tests can read back the result fields. Registered at runtime in
// initTestCase() so queued connections also work (none of these tests queue,
// but registration is harmless and matches production expectations).
Q_DECLARE_METATYPE(PendingResult)

// Capturing spdlog sink — collects every formatted log record so the
// unknown-id test can assert the WARN line. Installed as the default logger's
// only sink in initTestCase() (same pattern as MessageDispatcherTest).
class CapturingSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        messages.push_back(fmt::to_string(formatted));
    }
    void flush_() override {}

public:
    std::vector<std::string> messages;
};

// PendingRequestsTest (T10) — exercises the pending-request table:
//   - response within timeout resolves with success,
//   - 10s (here: 200ms) timeout resolves with timedOut=true,
//   - unknown id response is logged + dropped (no resolve, no throw),
//   - multiple concurrent expectations resolve independently by id,
//   - re-registering an id replaces the old expectation.
//
// QTEST_MAIN (NOT APPLESS): QTimer requires a QCoreApplication event loop,
// which the timeout test pumps via QSignalSpy::wait. handleResponse runs on
// the same thread in these tests so invokeMethod(AutoConnection) is a direct
// call and resolution is synchronous — the response tests need no qWait.
class PendingRequestsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testResponseWithinTimeout();
    void testTimeout();
    void testUnknownIdDropped();
    void testMultiplePending();
    void testReplace();

private:
    std::shared_ptr<CapturingSink> m_sink;
};

void PendingRequestsTest::initTestCase()
{
    qRegisterMetaType<PendingResult>("PendingResult");
    // Route all LOG_* output through a capturing sink so the unknown-id test
    // can inspect the WARN record. Process-global for the test exe;
    // cleanupTestCase() shuts spdlog down to leave a clean slate.
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("test_pending", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void PendingRequestsTest::cleanupTestCase()
{
    spdlog::shutdown();
}

namespace {
// Pop the oldest resolved() emission and return its (id, result). Caller must
// have already asserted the spy holds at least one record.
struct ResolvedEvent { QString id; PendingResult result; };
ResolvedEvent takeResolved(QSignalSpy& spy) {
    QList<QVariant> args = spy.takeFirst();
    return { args.at(0).toString(), qvariant_cast<PendingResult>(args.at(1)) };
}
} // namespace

// 1. A response arriving before the timeout resolves the expectation with the
//    response's success/error fields. No timeout fires.
void PendingRequestsTest::testResponseWithinTimeout()
{
    PendingRequests pr;
    QSignalSpy spy(&pr, &PendingRequests::resolved);
    QVERIFY(spy.isValid());

    pr.expectResponse(QStringLiteral("a"), 5000); // 5s, but response is instant
    QCOMPARE(pr.pendingCount(), 1);

    pr.handleResponse(createResponse(
        QStringLiteral("a"), QStringLiteral("load_model"),
        /*success=*/true));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(pr.pendingCount(), 0); // entry cleaned up on resolve

    const ResolvedEvent ev = takeResolved(spy);
    QCOMPARE(ev.id, QStringLiteral("a"));
    QVERIFY2(!ev.result.timedOut, "response must not be flagged as timed out");
    QCOMPARE(ev.result.success, true);
    QCOMPARE(ev.result.errorCode, 0);
}

// 2. No response within the per-entry timeout → resolved with timedOut=true.
//    Uses a 200ms timeout + QSignalSpy::wait to keep ctest fast.
void PendingRequestsTest::testTimeout()
{
    PendingRequests pr;
    QSignalSpy spy(&pr, &PendingRequests::resolved);
    QVERIFY(spy.isValid());

    pr.expectResponse(QStringLiteral("b"), 200);
    QCOMPARE(pr.pendingCount(), 1);

    // Pump the event loop until resolved fires (timer expires at ~200ms).
    QVERIFY2(spy.wait(2000),
             "resolved() should fire when the per-entry timer expires");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(pr.pendingCount(), 0);

    const ResolvedEvent ev = takeResolved(spy);
    QCOMPARE(ev.id, QStringLiteral("b"));
    QVERIFY2(ev.result.timedOut,
             "a timed-out expectation must carry timedOut=true");
    QCOMPARE(ev.result.success, false);
}

// 3. A response for an id with no pending expectation is logged + dropped —
//    no resolved() emission, no throw, pendingCount stays 0.
void PendingRequestsTest::testUnknownIdDropped()
{
    PendingRequests pr;
    QSignalSpy spy(&pr, &PendingRequests::resolved);
    QVERIFY(spy.isValid());
    m_sink->messages.clear();

    QCOMPARE(pr.pendingCount(), 0);
    bool threw = false;
    try {
        pr.handleResponse(createResponse(
            QStringLiteral("never-expected"), QStringLiteral("load_model"),
            /*success=*/true));
    } catch (...) {
        threw = true;
    }
    QVERIFY2(!threw, "handleResponse for an unknown id must not throw");
    QCOMPARE(spy.count(), 0);          // no resolution
    QCOMPARE(pr.pendingCount(), 0);    // nothing was pending to clean up

    // The drop was logged at WARN with the unknown id + the word "dropping".
    bool sawWarn = false;
    for (const std::string& line : m_sink->messages) {
        if (line.find("never-expected") != std::string::npos
            && line.find("dropping") != std::string::npos) {
            sawWarn = true;
            break;
        }
    }
    QVERIFY2(sawWarn,
             "expected a WARN log line for the unknown id "
             "(id='never-expected' + 'dropping')");
}

// 4. Multiple concurrent expectations resolve independently by id. Resolving
//    one does not disturb the others.
void PendingRequestsTest::testMultiplePending()
{
    PendingRequests pr;
    QSignalSpy spy(&pr, &PendingRequests::resolved);
    QVERIFY(spy.isValid());

    pr.expectResponse(QStringLiteral("a"));
    pr.expectResponse(QStringLiteral("b"));
    QCOMPARE(pr.pendingCount(), 2);

    // Resolve "a" first — "b" stays pending.
    pr.handleResponse(createResponse(
        QStringLiteral("a"), QStringLiteral("load_model"), /*success=*/true));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(pr.pendingCount(), 1);
    QCOMPARE(takeResolved(spy).id, QStringLiteral("a"));

    // Resolve "b".
    pr.handleResponse(createResponse(
        QStringLiteral("b"), QStringLiteral("set_fps"), /*success=*/false,
        /*errorCode=*/1004, /*errorMessage=*/"bad fps"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(pr.pendingCount(), 0);

    const ResolvedEvent ev = takeResolved(spy);
    QCOMPARE(ev.id, QStringLiteral("b"));
    QCOMPARE(ev.result.success, false);
    QCOMPARE(ev.result.errorCode, 1004);
    QCOMPARE(ev.result.errorMessage, QStringLiteral("bad fps"));
}

// 5. Re-registering the same id replaces the old expectation: the old entry is
//    resolved with timedOut=true (its timer cancelled), and the new entry
//    remains pending. The subsequent response resolves the NEW entry exactly
//    once — never a double-fire.
void PendingRequestsTest::testReplace()
{
    PendingRequests pr;
    QSignalSpy spy(&pr, &PendingRequests::resolved);
    QVERIFY(spy.isValid());

    pr.expectResponse(QStringLiteral("a"), 5000);
    QCOMPARE(pr.pendingCount(), 1);

    // Re-register: old expectation resolved with timedOut=true, new one in place.
    pr.expectResponse(QStringLiteral("a"), 5000);
    QCOMPARE(pr.pendingCount(), 1); // still exactly one pending (the new one)
    QCOMPARE(spy.count(), 1);       // the OLD entry's timeout-resolution

    const ResolvedEvent oldEv = takeResolved(spy);
    QCOMPARE(oldEv.id, QStringLiteral("a"));
    QVERIFY2(oldEv.result.timedOut,
             "the replaced (old) expectation must be resolved with timedOut=true");

    // The response now resolves the NEW expectation.
    pr.handleResponse(createResponse(
        QStringLiteral("a"), QStringLiteral("load_model"), /*success=*/true));
    QCOMPARE(spy.count(), 1);       // exactly one resolution from the inject
    QCOMPARE(pr.pendingCount(), 0);

    const ResolvedEvent newEv = takeResolved(spy);
    QCOMPARE(newEv.id, QStringLiteral("a"));
    QVERIFY2(!newEv.result.timedOut,
             "the replacement expectation must resolve via the response, not timeout");
    QCOMPARE(newEv.result.success, true);
}

QTEST_MAIN(PendingRequestsTest)
#include "PendingRequestsTest.moc"
