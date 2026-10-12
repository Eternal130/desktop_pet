// EventApiProxyTest (S8a, v1.5) — the per-plugin EventApiProxy contract,
// the shared EventApiImpl seams, the queryApi("pet.events") routing, and
// the EventCatalog builder key discipline. (Spec D, "EventApiProxyTest
// 或并入 PluginQueryApiTest" — kept as its own binary to leave the
// existing PluginQueryApiTest untouched.)
//
// Slots:
//   - plugin publish on pet.*            → InvalidArgument, ZERO dispatch
//   - plugin publish on plugin.<own>.*   → Ok, publisherId force-stamped
//   - plugin publish on plugin.<other>.* → InvalidArgument, zero dispatch
//   - proxy subscribe books the plugin id (unload sweep finds it)
//   - host-channel publish forwards publisherId VERBATIM (no overwrite)
//   - queryApi routing: injected → proxy / minVersion above → nullptr /
//     not wired → nullptr (the pet.model read-family precedent)
//   - EventCatalog builders emit the exact EventIds.hpp key tables
//
// The queryApi slots construct a real PluginContextImpl over the same
// minimal harness PluginQueryApiTest uses (WsServer + PendingRequests +
// InstanceManager on a QTemporaryDir) — the context ctor's shared-API
// pointers are documented non-null in production wiring.
// QTEST_MAIN — the InstanceManager harness touches QSqlDatabase (it
// wants a QCoreApplication instance), matching PluginQueryApiTest.

#include "api/EventIds.hpp"
#include "api/IEventApi.hpp"
#include "core/EventBus.hpp"
#include "core/EventCatalog.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceManager.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

struct RecordingListener : public pet::IEventListener
{
    int calls = 0;
    pet::Event lastEvent;

    void onEvent(pet::Event& event) override
    {
        ++calls;
        lastEvent = event;
    }
};

pet::Event makeEvent(const QString& id)
{
    pet::Event evt;
    evt.eventId = id;
    evt.instanceUuid = QStringLiteral("uuid-1");
    evt.payload.insert(QStringLiteral("marker"), 1);
    return evt;
}

} // namespace

class EventApiProxyTest : public QObject
{
    Q_OBJECT

private slots:
    void testPluginPublishPetNamespaceRejected();
    void testPluginPublishOwnNamespaceOkAndStamped();
    void testPluginPublishForeignPluginNamespaceRejected();
    void testProxySubscribeBooksPublisher();
    void testHostPublishForwardsPublisherIdVerbatim();
    void testQueryApiRouting();
    void testCatalogBuilderKeys();
};

void EventApiProxyTest::testPluginPublishPetNamespaceRejected()
{
    core::EventBus bus;
    core::EventApiImpl impl(&bus);
    core::EventApiProxy proxy(QStringLiteral("alpha"), &impl);

    RecordingListener listener;
    QCOMPARE(bus.subscribe(pet::kEventInstanceHit.toString(), &listener,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);

    pet::Event evt = makeEvent(pet::kEventInstanceHit.toString());
    QCOMPARE(proxy.publish(evt), pet::PluginError::InvalidArgument);
    QCOMPARE(listener.calls, 0); // zero dispatch
}

void EventApiProxyTest::testPluginPublishOwnNamespaceOkAndStamped()
{
    core::EventBus bus;
    core::EventApiImpl impl(&bus);
    core::EventApiProxy proxy(QStringLiteral("alpha"), &impl);

    RecordingListener listener;
    const QString channel = QStringLiteral("plugin.alpha.rule_fired");
    QCOMPARE(bus.subscribe(channel, &listener,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);

    pet::Event evt = makeEvent(channel);
    evt.publisherId = QStringLiteral("forged-host"); // must be overwritten
    QCOMPARE(proxy.publish(evt), pet::PluginError::Ok);
    QCOMPARE(listener.calls, 1);
    QCOMPARE(listener.lastEvent.publisherId, QStringLiteral("alpha"));
    QVERIFY(listener.lastEvent.timestampMs > 0); // bus stamped
}

void EventApiProxyTest::testPluginPublishForeignPluginNamespaceRejected()
{
    core::EventBus bus;
    core::EventApiImpl impl(&bus);
    core::EventApiProxy proxy(QStringLiteral("alpha"), &impl);

    RecordingListener listener;
    const QString channel = QStringLiteral("plugin.beta.rule_fired");
    QCOMPARE(bus.subscribe(channel, &listener,
                           pet::EventPriority::Normal, QString()),
             pet::PluginError::Ok);

    pet::Event evt = makeEvent(channel);
    QCOMPARE(proxy.publish(evt), pet::PluginError::InvalidArgument);
    QCOMPARE(listener.calls, 0); // zero dispatch
}

void EventApiProxyTest::testProxySubscribeBooksPublisher()
{
    core::EventBus bus;
    core::EventApiImpl impl(&bus);
    core::EventApiProxy proxy(QStringLiteral("alpha"), &impl);

    RecordingListener listener;
    QCOMPARE(proxy.subscribe(QStringLiteral("plugin.alpha.tick"), &listener,
                             pet::EventPriority::High),
             pet::PluginError::Ok);
    QCOMPARE(bus.subscriptionCount(), 1);

    // The unload sweep (PluginHost::shutdownAll) finds the entry by the
    // booked plugin id and removes it.
    QCOMPARE(impl.unsubscribeByPublisher(QStringLiteral("alpha")), 1);
    QCOMPARE(bus.subscriptionCount(), 0);

    // After the sweep the listener no longer receives.
    pet::Event evt = makeEvent(QStringLiteral("plugin.alpha.tick"));
    QCOMPARE(impl.publish(evt), pet::PluginError::Ok);
    QCOMPARE(listener.calls, 0);
}

void EventApiProxyTest::testHostPublishForwardsPublisherIdVerbatim()
{
    core::EventBus bus;
    core::EventApiImpl impl(&bus);

    RecordingListener listener;
    QCOMPARE(impl.subscribe(pet::kEventPackMounted.toString(), &listener,
                            pet::EventPriority::Normal),
             pet::PluginError::Ok);

    pet::Event evt = makeEvent(pet::kEventPackMounted.toString());
    evt.publisherId = QString(pet::kEventPublisherHost);
    QCOMPARE(impl.publish(evt), pet::PluginError::Ok);
    QCOMPARE(listener.lastEvent.publisherId,
             QString(pet::kEventPublisherHost)); // forwarded, not touched
}

void EventApiProxyTest::testQueryApiRouting()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [](const PanelConfig&) {}, nullptr);
    core::InstanceApiImpl instanceApi(&mgr, nullptr);
    core::InstanceControlApiImpl controlApi(&mgr, nullptr);

    core::EventBus bus;
    core::EventApiImpl eventApi(&bus);

    // Injected → the context serves its per-plugin proxy (a distinct
    // pet::IEventApi object, NOT the raw shared impl).
    std::unique_ptr<core::PluginContextImpl> ctx(
        new core::PluginContextImpl(
            QStringLiteral("alpha"), &instanceApi, &controlApi,
            /*writeEnabledProvider=*/{}, /*pageModel=*/nullptr,
            /*stream=*/nullptr, base.path(), /*downloadService=*/nullptr,
            /*capabilities=*/{}, /*voicePackRefresh=*/{}, nullptr));
    ctx->setEventApi(&eventApi);

    auto* served = dynamic_cast<pet::IEventApi*>(
        ctx->queryApi(pet::kEventsApiId, 1));
    QVERIFY(served != nullptr);
    QVERIFY(served != &eventApi); // the proxy, never the raw shared impl

    // minVersion above the family version → nullptr ("feature absent").
    QVERIFY(ctx->queryApi(pet::kEventsApiId, 2) == nullptr);

    // The proxy served through queryApi enforces the prefix rule.
    RecordingListener listener;
    QCOMPARE(served->subscribe(QStringLiteral("plugin.alpha.tick"), &listener,
                               pet::EventPriority::Normal),
             pet::PluginError::Ok);
    pet::Event evt = makeEvent(pet::kEventInstanceHit.toString());
    QCOMPARE(served->publish(evt), pet::PluginError::InvalidArgument);

    // Not wired → nullptr (the pet.model read-family precedent). A
    // second context without the injection is enough to prove it.
    core::PluginContextImpl bare(QStringLiteral("beta"), &instanceApi,
                                 &controlApi, {}, nullptr, nullptr,
                                 base.path(), nullptr, {}, {}, nullptr);
    QVERIFY(bare.queryApi(pet::kEventsApiId, 1) == nullptr);
}

void EventApiProxyTest::testCatalogBuilderKeys()
{
    // hit — events.md §hit keys, letter for letter.
    const QJsonObject hit = core::makeHitPayload(
        QStringLiteral("head"), 12.5, -3.25, 0);
    QCOMPARE(hit.size(), 4);
    QCOMPARE(hit.value(QStringLiteral("area_id")).toString(),
             QStringLiteral("head"));
    QCOMPARE(hit.value(QStringLiteral("x")).toDouble(), 12.5);
    QCOMPARE(hit.value(QStringLiteral("y")).toDouble(), -3.25);
    QCOMPARE(hit.value(QStringLiteral("button")).toInt(), 0);

    // motion_finished variant A: motion_path "" sentinel.
    const QJsonObject finA = core::makeMotionFinishedPayload(
        QStringLiteral("TapBody"), 0);
    QCOMPARE(finA.value(QStringLiteral("group")).toString(),
             QStringLiteral("TapBody"));
    QCOMPARE(finA.value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(finA.value(QStringLiteral("motion_path")).toString(),
             QStringLiteral(""));

    // motion_finished variant B: group ""/index -1 sentinels.
    const QJsonObject finB = core::makeMotionFinishedPayload(
        QStringLiteral("motions/tap.motion3.json"));
    QCOMPARE(finB.value(QStringLiteral("group")).toString(),
             QStringLiteral(""));
    QCOMPARE(finB.value(QStringLiteral("index")).toInt(), -1);
    QCOMPARE(finB.value(QStringLiteral("motion_path")).toString(),
             QStringLiteral("motions/tap.motion3.json"));

    // model_loaded — host-supplemented metadata halves.
    const QJsonObject loaded = core::makeModelLoadedPayload(
        QStringLiteral("Hiyori"),
        QStringList{ QStringLiteral("Idle"), QStringLiteral("TapBody") },
        QStringList{ QStringLiteral("f01") });
    QCOMPARE(loaded.value(QStringLiteral("model_id")).toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE((loaded.value(QStringLiteral("motion_groups")).toArray()),
             (QJsonArray{ QStringLiteral("Idle"), QStringLiteral("TapBody") }));
    QCOMPARE((loaded.value(QStringLiteral("expressions")).toArray()),
             (QJsonArray{ QStringLiteral("f01") }));

    // drag pair + ready verbatim + idle tick + failure + pack.
    const QJsonObject dragEnd = core::makeDragEndedPayload(1, 2, 100, 200);
    QCOMPARE(dragEnd.value(QStringLiteral("x")).toDouble(), 1.0);
    QCOMPARE(dragEnd.value(QStringLiteral("y")).toDouble(), 2.0);
    QCOMPARE(dragEnd.value(QStringLiteral("window_x")).toInt(), 100);
    QCOMPARE(dragEnd.value(QStringLiteral("window_y")).toInt(), 200);

    QJsonObject readyPayload;
    readyPayload.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    readyPayload.insert(QStringLiteral("capabilities"),
                        QJsonArray{ QStringLiteral("live2d") });
    const QJsonObject ready = core::makeReadyPayload(readyPayload);
    QCOMPARE(ready, readyPayload); // verbatim copy

    const QJsonObject idle = core::makeIdleTickPayload(
        QStringLiteral("Idle"), 3);
    QCOMPARE(idle.value(QStringLiteral("group")).toString(),
             QStringLiteral("Idle"));
    QCOMPARE(idle.value(QStringLiteral("index")).toInt(), 3);

    const QJsonObject fail = core::makeModelLoadFailedPayload(
        5001, QStringLiteral("missing model"));
    QCOMPARE(fail.value(QStringLiteral("error_code")).toInt(), 5001);
    QCOMPARE(fail.value(QStringLiteral("error_message")).toString(),
             QStringLiteral("missing model"));

    const QJsonObject pack = core::makePackPayload(QStringLiteral("pack_v1"));
    QCOMPARE(pack.size(), 1);
    QCOMPARE(pack.value(QStringLiteral("pack_id")).toString(),
             QStringLiteral("pack_v1"));
}

QTEST_MAIN(EventApiProxyTest)

#include "EventApiProxyTest.moc"
