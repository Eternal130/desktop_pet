// EventBusTest (S8a, v1.5) — the bus CORE contract, exercised directly
// on core::EventBus (no PluginContextImpl / host machinery in the loop).
//
// Slots (spec D, EventBusTest):
//   - priority dispatch order (Highest→Lowest; same-priority FIFO stable)
//   - cancellable chain (mid listener cancels → later listeners still
//     receive; poster reads the final cancelled=true)
//   - non-cancellable: a listener writing cancelled is ignored
//   - re-entrancy: 9 nested publishes → the 9th returns Busy
//   - event-ID syntax table (empty segment / uppercase / wildcard /
//     2 segments) — table-driven over isValidEventId + subscribe
//   - unknown eventId subscription is legal (subscribe-before-publish)
//   - unsubscribing from inside the fanout does not crash (snapshot)
//   - a throwing listener is isolated; later listeners still receive
//   - unsubscribeByPublisher sweep
//   - duplicate (eventId, listener) subscribe merges + updates priority
//
// QTEST_APPLESS_MAIN — the bus is synchronous; no event loop needed.
// Links pet_panel_core (EventBus lives there).

#include "api/EventIds.hpp"
#include "core/EventBus.hpp"

#include <QString>
#include <QStringList>
#include <QTest>

#include <exception>

namespace {

// Recording listener: appends its tag to a shared order log, optionally
// cancels / throws / re-publishes / unsubscribes inside onEvent.
struct RecordingListener : public pet::IEventListener
{
    QString tag;
    QStringList* log = nullptr;
    bool cancel = false;
    bool throwIt = false;
    pet::Event lastEvent;

    void onEvent(pet::Event& event) override
    {
        lastEvent = event;
        if (log != nullptr)
            log->append(tag);
        if (throwIt)
            throw std::runtime_error("listener boom");
        if (cancel)
            event.cancelled = true;
    }
};

} // namespace

class EventBusTest : public QObject
{
    Q_OBJECT

private slots:
    void testPriorityDispatchOrder();
    void testSamePriorityFifoStable();
    void testCancellableChain();
    void testNonCancellableWriteIgnored();
    void testReentrancyDepthNineReturnsBusy();
    void testEventIdSyntaxTable();
    void testUnknownEventIdSubscribeOk();
    void testUnsubscribeDuringDispatchDoesNotCrash();
    void testThrowingListenerIsolated();
    void testUnsubscribeByPublisherSweep();
    void testDuplicateSubscribeMerges();
    void testPublishStampsTimestamp();
};

void EventBusTest::testPriorityDispatchOrder()
{
    core::EventBus bus;
    QStringList log;
    RecordingListener low, normal, highest;
    low.tag = QStringLiteral("low");
    normal.tag = QStringLiteral("normal");
    highest.tag = QStringLiteral("highest");
    low.log = normal.log = highest.log = &log;

    QVERIFY(bus.subscribe(QStringLiteral("pet.test.order"), &low,
                          pet::EventPriority::Low, QString())
            == pet::PluginError::Ok);
    QVERIFY(bus.subscribe(QStringLiteral("pet.test.order"), &normal,
                          pet::EventPriority::Normal, QString())
            == pet::PluginError::Ok);
    QVERIFY(bus.subscribe(QStringLiteral("pet.test.order"), &highest,
                          pet::EventPriority::Highest, QString())
            == pet::PluginError::Ok);

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.order");
    QVERIFY(bus.publish(evt) == pet::PluginError::Ok);
    QCOMPARE(log, QStringList({ QStringLiteral("highest"),
                                QStringLiteral("normal"),
                                QStringLiteral("low") }));
}

void EventBusTest::testSamePriorityFifoStable()
{
    core::EventBus bus;
    QStringList log;
    RecordingListener a, b, c;
    a.tag = QStringLiteral("a");
    b.tag = QStringLiteral("b");
    c.tag = QStringLiteral("c");
    a.log = b.log = c.log = &log;

    // Subscribe in a-b-c order at the SAME priority; re-subscribing `a`
    // (merge) must NOT move it to the back — stable order is append
    // order among equals.
    bus.subscribe(QStringLiteral("pet.test.fifo"), &a,
                  pet::EventPriority::Normal, QString());
    bus.subscribe(QStringLiteral("pet.test.fifo"), &b,
                  pet::EventPriority::Normal, QString());
    bus.subscribe(QStringLiteral("pet.test.fifo"), &c,
                  pet::EventPriority::Normal, QString());
    bus.subscribe(QStringLiteral("pet.test.fifo"), &a,
                  pet::EventPriority::Normal, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.fifo");
    bus.publish(evt);
    QCOMPARE(log, QStringList({ QStringLiteral("a"),
                                QStringLiteral("b"),
                                QStringLiteral("c") }));
}

void EventBusTest::testCancellableChain()
{
    core::EventBus bus;
    QStringList log;
    RecordingListener first, canceller, last;
    first.tag = QStringLiteral("first");
    canceller.tag = QStringLiteral("canceller");
    last.tag = QStringLiteral("last");
    first.log = canceller.log = last.log = &log;
    canceller.cancel = true;

    bus.subscribe(QStringLiteral("pet.test.cancel"), &first,
                  pet::EventPriority::High, QString());
    bus.subscribe(QStringLiteral("pet.test.cancel"), &canceller,
                  pet::EventPriority::Normal, QString());
    bus.subscribe(QStringLiteral("pet.test.cancel"), &last,
                  pet::EventPriority::Low, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.cancel");
    evt.cancellable = true;
    QVERIFY(bus.publish(evt) == pet::PluginError::Ok);
    // All three received (cancellation shortens nothing mid-chain).
    QCOMPARE(log, QStringList({ QStringLiteral("first"),
                                QStringLiteral("canceller"),
                                QStringLiteral("last") }));
    // The poster reads the final value.
    QVERIFY(evt.cancelled);
}

void EventBusTest::testNonCancellableWriteIgnored()
{
    core::EventBus bus;
    RecordingListener canceller;
    canceller.cancel = true;

    bus.subscribe(QStringLiteral("pet.test.nocancel"), &canceller,
                  pet::EventPriority::Normal, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.nocancel");
    evt.cancellable = false; // non-cancellable by construction
    QVERIFY(bus.publish(evt) == pet::PluginError::Ok);
    // The listener's write was void — no error, final value false.
    QVERIFY(!evt.cancelled);
}

namespace {

// Re-entrant listener: every onEvent publishes the same channel again,
// recording each publish's result; stops at the first Busy.
struct RecursiveListener : public pet::IEventListener
{
    core::EventBus* bus = nullptr;
    QList<pet::PluginError> results;

    void onEvent(pet::Event& event) override
    {
        pet::Event nested = event; // same id, fresh copy
        results.append(bus->publish(nested));
    }
};

} // namespace

void EventBusTest::testReentrancyDepthNineReturnsBusy()
{
    core::EventBus bus;
    RecursiveListener listener;
    listener.bus = &bus;
    bus.subscribe(QStringLiteral("pet.test.deep"), &listener,
                  pet::EventPriority::Normal, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.deep");
    const pet::PluginError top = bus.publish(evt);

    // Depth chain: top publish (depth 1, Ok) + one nested publish per
    // delivered listener. Levels 1..8 succeed; the 9th returns Busy.
    // (Appends happen on the UNWIND, so count by value, not position.)
    QCOMPARE(top, pet::PluginError::Ok);
    QCOMPARE(listener.results.size(), core::kEventMaxPublishDepth);
    int busyCount = 0;
    int okCount = 0;
    for (const pet::PluginError r : listener.results) {
        if (r == pet::PluginError::Busy)
            ++busyCount;
        else if (r == pet::PluginError::Ok)
            ++okCount;
    }
    QCOMPARE(busyCount, 1); // exactly one level hit the cap
    QCOMPARE(okCount, core::kEventMaxPublishDepth - 1);
}

void EventBusTest::testEventIdSyntaxTable()
{
    struct Row {
        const char* id;
        bool valid;
    };
    const Row rows[] = {
        { "pet.instance.hit", true },
        { "plugin.my_plug.rule_fired", true },
        { "pet.model.load_failed", true },
        { "pet.future.channel", true },
        { "", false },                // empty
        { "pet.hit", false },         // 2 segments
        { "pet", false },             // 1 segment
        { "pet..hit", false },        // empty middle segment
        { ".pet.instance.hit", false }, // leading dot = empty segment
        { "pet.instance.hit.", false }, // trailing dot = empty segment
        { "Pet.instance.hit", false },  // uppercase
        { "pet.Instance.hit", false },  // uppercase segment
        { "pet.instance.*", false },    // wildcard
        { "pet.*.hit", false },         // wildcard segment
        { "pet.instance.hi!", false },  // illegal character
        { "pet.instance.hi-", false },  // illegal character
        { "pet.instance. hit", false }, // space
    };
    for (const Row& row : rows) {
        const QString id = QString::fromLatin1(row.id);
        QVERIFY2(core::EventBus::isValidEventId(id) == row.valid,
                 qPrintable(id));
    }

    // The bus enforces the same table at subscribe AND publish.
    core::EventBus bus;
    RecordingListener listener;
    for (const Row& row : rows) {
        if (row.valid)
            continue;
        const QString id = QString::fromLatin1(row.id);
        QCOMPARE(bus.subscribe(id, &listener,
                               pet::EventPriority::Normal, QString()),
                 pet::PluginError::InvalidArgument);
        pet::Event evt;
        evt.eventId = id;
        QCOMPARE(bus.publish(evt), pet::PluginError::InvalidArgument);
    }
    // Null listener is InvalidArgument too.
    QCOMPARE(bus.subscribe(QStringLiteral("pet.test.null"), nullptr,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::InvalidArgument);
}

void EventBusTest::testUnknownEventIdSubscribeOk()
{
    core::EventBus bus;
    RecordingListener listener;
    // Subscribe-before-publish: the channel does not exist yet anywhere.
    QCOMPARE(bus.subscribe(QStringLiteral("pet.future.thing"), &listener,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.future.thing");
    QCOMPARE(bus.publish(evt), pet::PluginError::Ok);
    QCOMPARE(listener.lastEvent.eventId, QStringLiteral("pet.future.thing"));
}

namespace {

// Listener that unsubscribes itself (and optionally a peer) mid-fanout.
struct SelfUnsubscribingListener : public pet::IEventListener
{
    core::EventBus* bus = nullptr;
    pet::IEventListener* peer = nullptr; // also unsubscribed when set
    int calls = 0;
    bool cancel = false;

    void onEvent(pet::Event& event) override
    {
        ++calls;
        bus->unsubscribe(QStringLiteral("pet.test.midremove"), this);
        if (peer != nullptr)
            bus->unsubscribe(QStringLiteral("pet.test.midremove"), peer);
        if (cancel)
            event.cancelled = true;
    }
};

} // namespace

void EventBusTest::testUnsubscribeDuringDispatchDoesNotCrash()
{
    core::EventBus bus;
    SelfUnsubscribingListener remover;
    remover.bus = &bus;
    RecordingListener peer;
    remover.peer = &peer; // the remover also unsubscribes its peer mid-fanout
    bus.subscribe(QStringLiteral("pet.test.midremove"), &remover,
                  pet::EventPriority::Highest, QString());
    bus.subscribe(QStringLiteral("pet.test.midremove"), &peer,
                  pet::EventPriority::Lowest, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.midremove");
    QVERIFY(bus.publish(evt) == pet::PluginError::Ok); // no crash
    QCOMPARE(remover.calls, 1);
    // Snapshot semantics: the peer was still in the snapshot → visited.
    QCOMPARE(peer.lastEvent.eventId, QStringLiteral("pet.test.midremove"));
    // Both entries are gone; a second publish delivers to nobody.
    QCOMPARE(bus.subscriptionCount(), 0);
}

void EventBusTest::testThrowingListenerIsolated()
{
    core::EventBus bus;
    RecordingListener thrower, survivor;
    thrower.throwIt = true;
    thrower.tag = QStringLiteral("thrower");
    survivor.tag = QStringLiteral("survivor");
    QStringList log;
    thrower.log = survivor.log = &log;

    bus.subscribe(QStringLiteral("pet.test.throw"), &thrower,
                  pet::EventPriority::Highest, QString());
    bus.subscribe(QStringLiteral("pet.test.throw"), &survivor,
                  pet::EventPriority::Normal, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.throw");
    QVERIFY(bus.publish(evt) == pet::PluginError::Ok);
    // Thrower ran (and threw, was logged + isolated); survivor still ran.
    QCOMPARE(log, QStringList({ QStringLiteral("thrower"),
                                QStringLiteral("survivor") }));
}

void EventBusTest::testUnsubscribeByPublisherSweep()
{
    core::EventBus bus;
    RecordingListener plugA1, plugA2, plugB, hostSide;
    QCOMPARE(bus.subscribe(QStringLiteral("pet.test.sweep"), &plugA1,
                           pet::EventPriority::Normal,
                           QStringLiteral("plugin.alpha")),
             pet::PluginError::Ok);
    QCOMPARE(bus.subscribe(QStringLiteral("pet.test.other"), &plugA2,
                           pet::EventPriority::Normal,
                           QStringLiteral("plugin.alpha")),
             pet::PluginError::Ok);
    QCOMPARE(bus.subscribe(QStringLiteral("pet.test.sweep"), &plugB,
                           pet::EventPriority::Normal,
                           QStringLiteral("plugin.beta")),
             pet::PluginError::Ok);
    QCOMPARE(bus.subscribe(QStringLiteral("pet.test.sweep"), &hostSide,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);
    QCOMPARE(bus.subscriptionCount(), 4);

    // Sweep plugin.alpha only: both its entries (across channels) go,
    // beta + host survive.
    QCOMPARE(bus.unsubscribeByPublisher(QStringLiteral("plugin.alpha")), 2);
    QCOMPARE(bus.subscriptionCount(), 2);

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.sweep");
    bus.publish(evt);
    QVERIFY(plugA1.lastEvent.eventId.isEmpty());   // not delivered
    QVERIFY(plugA2.lastEvent.eventId.isEmpty());
    QCOMPARE(plugB.lastEvent.eventId, QStringLiteral("pet.test.sweep"));
    QCOMPARE(hostSide.lastEvent.eventId, QStringLiteral("pet.test.sweep"));
}

void EventBusTest::testDuplicateSubscribeMerges()
{
    core::EventBus bus;
    QStringList log;
    RecordingListener first, second, mover;
    first.tag = QStringLiteral("first");
    second.tag = QStringLiteral("second");
    mover.tag = QStringLiteral("mover");
    first.log = second.log = mover.log = &log;

    bus.subscribe(QStringLiteral("pet.test.merge"), &first,
                  pet::EventPriority::Normal, QString());
    bus.subscribe(QStringLiteral("pet.test.merge"), &mover,
                  pet::EventPriority::Low, QString());
    // Mover re-subscribes with a HIGHER priority — merge must move its
    // position, not duplicate the entry.
    bus.subscribe(QStringLiteral("pet.test.merge"), &mover,
                  pet::EventPriority::Highest, QString());
    QCOMPARE(bus.subscriptionCount(), 2);

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.merge");
    bus.publish(evt);
    QCOMPARE(log, QStringList({ QStringLiteral("mover"),
                                QStringLiteral("first") }));
}

void EventBusTest::testPublishStampsTimestamp()
{
    core::EventBus bus;
    RecordingListener listener;
    bus.subscribe(QStringLiteral("pet.test.stamp"), &listener,
                  pet::EventPriority::Normal, QString());

    pet::Event evt;
    evt.eventId = QStringLiteral("pet.test.stamp");
    evt.timestampMs = -12345; // emitter garbage — must be overwritten
    bus.publish(evt);
    QVERIFY(evt.timestampMs > 0);
    QVERIFY(listener.lastEvent.timestampMs == evt.timestampMs);
}

QTEST_APPLESS_MAIN(EventBusTest)

#include "EventBusTest.moc"
