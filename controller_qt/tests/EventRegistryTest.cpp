#include <QJsonObject>
#include <QString>
#include <QTest>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include "logging/Logging.hpp"
#include "network/Envelope.hpp"
#include "network/EventRegistry.hpp"
#include "network/MessageDispatcher.hpp"

// Capturing spdlog sink — collects every formatted log record so the
// registerDefaults test can assert the default LOG_DEBUG handler fired for
// motion_finished (and that an override replaced it). Installed as the default
// logger's only sink in initTestCase(). Mirrors the MessageDispatcherTest T7
// pattern.
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

// EventRegistryTest (T11) — verifies the registry delegates correctly to
// MessageDispatcher (custom handler fires with parsed payload, pseudo-response
// stats_state routes by action even on id collision, unregister stops further
// calls, registerDefaults installs handlers for all 13 events and can be
// overridden). The dispatcher is used as a REAL dependency (constructed in
// each test, wired to a stack EventRegistry). QTEST_APPLESS_MAIN — no event
// loop; dispatch() is synchronous.
class EventRegistryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testCustomHandlerFiresOnDragEnd();
    void testStatsStateRoutesByActionNotId();
    void testUnregisterStopsHandler();
    void testRegisterDefaultsThenOverride();

private:
    std::shared_ptr<CapturingSink> m_sink;

    // True if any captured log line mentions `needle`.
    bool sawLogContaining(const std::string& needle) const;
};

void EventRegistryTest::initTestCase()
{
    m_sink = std::make_shared<CapturingSink>();
    auto logger = std::make_shared<spdlog::logger>("test_event_registry", m_sink);
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(logger);
}

void EventRegistryTest::cleanupTestCase()
{
    spdlog::shutdown();
}

bool EventRegistryTest::sawLogContaining(const std::string& needle) const
{
    for (const std::string& line : m_sink->messages) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 1. Custom handler fires: register drag_end → inject drag_end Envelope with
//    window_x=100, window_y=200 → assert handler called with parsed values.
void EventRegistryTest::testCustomHandlerFiresOnDragEnd()
{
    MessageDispatcher d;
    EventRegistry reg(&d);

    int calls = 0;
    double seenX = -1.0;
    double seenY = -1.0;
    reg.registerEventHandler(QStringLiteral("drag_end"),
        [&](const Envelope& env) {
            ++calls;
            // drag_end payload (interface.md §4.1.D-h): window_x, window_y.
            seenX = env.payload.value(QStringLiteral("window_x")).toDouble();
            seenY = env.payload.value(QStringLiteral("window_y")).toDouble();
        });

    d.dispatch(createEvent(QStringLiteral("drag_end"), QJsonObject{
        {QStringLiteral("window_x"), 100.0},
        {QStringLiteral("window_y"), 200.0},
    }));

    QCOMPARE(calls, 1);
    QCOMPARE(seenX, 100.0);
    QCOMPARE(seenY, 200.0);
}

// 2. stats_state routes by ACTION (pseudo-response, §7.3): even when its id
//    collides with a pending request id, the EVENT handler installed via the
//    registry fires and the response handler does NOT. This proves the
//    registry inherits the dispatcher's by-action routing — no special-casing
//    leaks into the wrapper.
void EventRegistryTest::testStatsStateRoutesByActionNotId()
{
    MessageDispatcher d;
    EventRegistry reg(&d);

    bool eventFired = false;
    bool responseFired = false;

    reg.registerEventHandler(QStringLiteral("stats_state"),
        [&eventFired](const Envelope& env) {
            eventFired = true;
            QCOMPARE(env.action, QStringLiteral("stats_state"));
        });
    // A response handler that would capture the message IF it were misrouted.
    d.setResponseHandler([&responseFired](const Envelope&) {
        responseFired = true;
    });

    // Deliberate id collision with a notional pending get_stats request —
    // exactly the event_all_13.json::layout_state_pseudo_response_id_collision
    // scenario. The dispatcher routes by env.type=="event" → action first, so
    // the id is irrelevant to routing.
    Envelope env = createEvent(QStringLiteral("stats_state"), QJsonObject{
        {QStringLiteral("cpu_percent"), 12.5},
        {QStringLiteral("rss_bytes"), 104857600},
    });
    env.id = QStringLiteral("pending-request-id");
    d.dispatch(env);

    QVERIFY2(eventFired,
        "stats_state MUST route to its event handler via the registry (by action)");
    QVERIFY2(!responseFired,
        "stats_state MUST NOT route to the response handler "
        "(§7.3: by action, never by id)");
}

// 3. Unregister: after unregisterEventHandler, a subsequent dispatch does not
//    invoke the previously-registered handler.
void EventRegistryTest::testUnregisterStopsHandler()
{
    MessageDispatcher d;
    EventRegistry reg(&d);

    int calls = 0;
    reg.registerEventHandler(QStringLiteral("drag_end"),
        [&calls](const Envelope&) { ++calls; });
    reg.unregisterEventHandler(QStringLiteral("drag_end"));

    d.dispatch(createEvent(QStringLiteral("drag_end")));
    QCOMPARE(calls, 0);
}

// 4. registerDefaults: installs handlers for all 13 events. motion_finished is
//    a Phase 6+ event → default is a LOG_DEBUG handler. Inject motion_finished
//    → assert the default handler fired (log line captured). Then OVERRIDE
//    with a custom handler → inject again → assert the custom handler fires
//    AND the default no longer logs (override replaced it).
void EventRegistryTest::testRegisterDefaultsThenOverride()
{
    MessageDispatcher d;
    EventRegistry reg(&d);
    reg.registerDefaults();

    // Phase 1 — default handler for motion_finished is installed.
    m_sink->messages.clear();
    d.dispatch(createEvent(QStringLiteral("motion_finished"), QJsonObject{
        {QStringLiteral("group"), QStringLiteral("TapBody")},
        {QStringLiteral("index"), 0},
    }));
    QVERIFY2(sawLogContaining("motion_finished"),
        "registerDefaults must install a handler for motion_finished "
        "(default LOG_DEBUG fired)");

    // Phase 2 — override replaces the default with a custom handler.
    int customCalls = 0;
    reg.registerEventHandler(QStringLiteral("motion_finished"),
        [&customCalls](const Envelope&) { ++customCalls; });
    m_sink->messages.clear();

    d.dispatch(createEvent(QStringLiteral("motion_finished")));
    QCOMPARE(customCalls, 1);
    QVERIFY2(!sawLogContaining("motion_finished"),
        "override must replace the default handler — the default LOG_DEBUG "
        "line must not fire after registerEventHandler overrides it");
}

QTEST_APPLESS_MAIN(EventRegistryTest)
#include "EventRegistryTest.moc"
