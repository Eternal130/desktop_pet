#include <QJsonObject>
#include <QString>
#include <QTest>

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/MessageDispatcher.hpp"

// Capturing spdlog sink — collects every formatted log record so the isolation
// test can assert that the throwing handler's exception was logged. Installed as
// the default logger's only sink in initTestCase().
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

// MessageDispatcherTest (T7) — pure-logic routing. dispatch() is invoked
// directly (it is a public slot AND a plain method). QTEST_APPLESS_MAIN since
// no event loop is required: the dispatcher routes synchronously.
class MessageDispatcherTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testEventRouting();
    void testResponseRouting();
    void testPseudoResponseRoutedByAction();
    void testHandlerIsolation();
    void testUnregisteredActionDoesNotThrow();
    void testUnregister();

private:
    std::shared_ptr<CapturingSink> m_sink;
};

void MessageDispatcherTest::initTestCase()
{
    // Route all LOG_* output through a capturing sink so the isolation test can
    // verify the error record. This default logger is process-global for the
    // test exe; cleanupTestCase() shuts spdlog down to leave a clean slate.
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("test_dispatcher", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void MessageDispatcherTest::cleanupTestCase()
{
    spdlog::shutdown();
}

// 1. A registered `hit` handler fires on a `hit` event with the right action.
void MessageDispatcherTest::testEventRouting()
{
    MessageDispatcher d;
    int hits = 0;
    QString seenAction;
    d.registerEventHandler(QStringLiteral("hit"),
                           [&hits, &seenAction](const Envelope& env) {
                               ++hits;
                               seenAction = env.action;
                           });

    const Envelope env = createEvent(QStringLiteral("hit"), QJsonObject{
        {QStringLiteral("area_id"), QStringLiteral("head")},
        {QStringLiteral("x"), 120.5},
        {QStringLiteral("y"), 80.0},
        {QStringLiteral("button"), 0},
    });
    d.dispatch(env);

    QCOMPARE(hits, 1);
    QCOMPARE(seenAction, QStringLiteral("hit"));
}

// 2. A set ResponseHandler fires on a response Envelope.
void MessageDispatcherTest::testResponseRouting()
{
    MessageDispatcher d;
    int calls = 0;
    QString seenId;
    d.setResponseHandler([&calls, &seenId](const Envelope& env) {
        ++calls;
        seenId = env.id;
    });

    const Envelope resp = createResponse(
        QStringLiteral("cmd-load-model-0001"),
        QStringLiteral("load_model"),
        /*success=*/true);
    d.dispatch(resp);

    QCOMPARE(calls, 1);
    QCOMPARE(seenId, QStringLiteral("cmd-load-model-0001"));
}

// 3. Pseudo-response (§7.3): stats_state is an EVENT and must route by ACTION,
//    NOT by id — even when its id would match a "pending request" id.
void MessageDispatcherTest::testPseudoResponseRoutedByAction()
{
    MessageDispatcher d;
    bool eventFired = false;
    bool responseFired = false;

    d.registerEventHandler(QStringLiteral("stats_state"),
                           [&eventFired](const Envelope& env) {
                               eventFired = true;
                               QCOMPARE(env.action, QStringLiteral("stats_state"));
                           });
    // A response handler that would capture the message IF it were misrouted.
    d.setResponseHandler([&responseFired](const Envelope&) {
        responseFired = true;
    });

    // Build a stats_state EVENT whose id deliberately COLLIDES with a notional
    // pending get_stats command id — exactly the scenario from
    // event_all_13.json::layout_state_pseudo_response_id_collision. A correct
    // dispatcher routes by env.type=="event" → action, ignoring the id.
    Envelope env = createEvent(QStringLiteral("stats_state"), QJsonObject{
        {QStringLiteral("cpu_percent"), 12.5},
        {QStringLiteral("rss_bytes"), 104857600},
    });
    env.id = QStringLiteral("matching-id"); // id collision is irrelevant
    d.dispatch(env);

    QVERIFY2(eventFired, "stats_state MUST route to its event handler (by action)");
    QVERIFY2(!responseFired,
             "stats_state MUST NOT route to the response handler "
             "(§7.3: by action, never by id)");
}

// 4. Handler isolation: a handler that throws std::runtime_error is caught, an
//    error is logged, and the NEXT event still routes to its handler.
void MessageDispatcherTest::testHandlerIsolation()
{
    MessageDispatcher d;
    m_sink->messages.clear(); // ignore setup noise

    int throwingCalls = 0;
    int secondCalls = 0;

    d.registerEventHandler(QStringLiteral("motion_finished"),
                           [&throwingCalls](const Envelope&) {
                               ++throwingCalls;
                               throw std::runtime_error("boom from motion_finished handler");
                           });
    d.registerEventHandler(QStringLiteral("hit"),
                           [&secondCalls](const Envelope&) { ++secondCalls; });

    // First: the throwing handler. Exception must be swallowed by dispatch().
    d.dispatch(createEvent(QStringLiteral("motion_finished"), QJsonObject{
        {QStringLiteral("group"), QStringLiteral("TapBody")},
        {QStringLiteral("index"), 0},
    }));
    // Second: a DIFFERENT event must still route (isolation proof).
    d.dispatch(createEvent(QStringLiteral("hit"), QJsonObject{
        {QStringLiteral("area_id"), QStringLiteral("body")},
    }));

    QCOMPARE(throwingCalls, 1); // throwing handler WAS invoked
    QCOMPARE(secondCalls, 1);   // isolation: next message still routed

    // The catch block logged an error mentioning the action + the message.
    bool sawIsolationError = false;
    for (const std::string& line : m_sink->messages) {
        if (line.find("motion_finished") != std::string::npos
            && line.find("threw exception") != std::string::npos) {
            sawIsolationError = true;
            break;
        }
    }
    QVERIFY2(sawIsolationError,
             "an error log line was expected for the throwing handler "
             "(action='motion_finished' + 'threw exception')");
}

// 5. Unregistered action: no handler, no crash, no throw (LOG_WARN is enough).
void MessageDispatcherTest::testUnregisteredActionDoesNotThrow()
{
    MessageDispatcher d;
    // No handler for "unknown_action" — dispatch must not throw. QVERIFY_NO_THROW
    // is not provided by this Qt 6.10 QTest build, so the catch is manual.
    const Envelope env = createEvent(QStringLiteral("unknown_action"));
    bool threw = false;
    try {
        d.dispatch(env);
    } catch (...) {
        threw = true;
    }
    QVERIFY2(!threw, "dispatch of an unregistered action must not throw");
}

// 6. unregisterEventHandler: after unregister, a previously-registered handler
//    does NOT fire on subsequent dispatch.
void MessageDispatcherTest::testUnregister()
{
    MessageDispatcher d;
    int calls = 0;
    d.registerEventHandler(QStringLiteral("drag_end"),
                           [&calls](const Envelope&) { ++calls; });
    d.unregisterEventHandler(QStringLiteral("drag_end"));

    d.dispatch(createEvent(QStringLiteral("drag_end")));
    QCOMPARE(calls, 0);
}

QTEST_APPLESS_MAIN(MessageDispatcherTest)
#include "MessageDispatcherTest.moc"
