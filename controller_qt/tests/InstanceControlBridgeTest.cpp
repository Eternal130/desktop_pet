// InstanceControlBridgeTest (S5, v1.3; layout forwards since v1.8) — the
// detail-page write bridge over the plugin interfaces, exercised through
// programmable fakes (same shape as RosterApiModelTest's FakeControlApi
// — pure interfaces, no core/ instance needed). Locks:
//   - testForwardsToTuningApi: every tuning Q_INVOKABLE forwards (uuid +
//      args verbatim) and returns the fake's PluginError code as an int.
//   - testForwardsToControlApi: start/stop/restart/loadModel forward
//      through the lifecycle API.
//   - testForwardsToLayoutApi (v1.8): setAutoStart/resetLayout forward
//      through the layout API; getLayout returns the QML-friendly
//      {ok:true,x,y,width,height,scale} map on success and the
//      {ok:false,error} shape on failure.
//   - testNullApisReturnGeneric: degraded wiring → Generic on every
//      Q_INVOKABLE (getLayout → {ok:false,error:Generic}), never a crash.
//
// QTEST_APPLESS_MAIN — pure synchronous forwards, no event loop needed.
// Links pet_panel_core (InstanceControlBridge lives there).

#include "api/IInstanceControlApi.hpp"
#include "api/ILayoutApi.hpp"
#include "api/ITuningApi.hpp"
#include "ui/InstanceControlBridge.hpp"

#include <QString>
#include <QTest>
#include <QVariantMap>

namespace {

// Programmable fake tuning API: records the last call per method + the
// error to answer with.
class FakeTuningApi : public pet::ITuningApi
{
public:
    pet::PluginError answer = pet::PluginError::Ok;
    QString lastUuid;
    double lastDouble = -1.0;
    bool lastBool = false;
    int lastInt = -1;
    QString lastGroup, lastExpr, lastArea, lastPackId;
    int calls = 0;

    pet::PluginError setOpacity(const QString& uuid, double opacity) override
    { ++calls; lastUuid = uuid; lastDouble = opacity; return answer; }
    pet::PluginError setVolume(const QString& uuid, double volume) override
    { ++calls; lastUuid = uuid; lastDouble = volume; return answer; }
    pet::PluginError setMuted(const QString& uuid, bool muted) override
    { ++calls; lastUuid = uuid; lastBool = muted; return answer; }
    pet::PluginError setFps(const QString& uuid, int fps) override
    { ++calls; lastUuid = uuid; lastInt = fps; return answer; }
    pet::PluginError playMotion(const QString& uuid, const QString& group,
                                int index) override
    { ++calls; lastUuid = uuid; lastGroup = group; lastInt = index; return answer; }
    pet::PluginError setExpression(const QString& uuid,
                                   const QString& expressionId) override
    { ++calls; lastUuid = uuid; lastExpr = expressionId; return answer; }
    pet::PluginError triggerHitArea(const QString& uuid,
                                    const QString& areaId) override
    { ++calls; lastUuid = uuid; lastArea = areaId; return answer; }
    pet::PluginError mountVoicePack(const QString& uuid,
                                    const QString& packId) override
    { ++calls; lastUuid = uuid; lastPackId = packId; return answer; }
    pet::PluginError unmountVoicePack(const QString& uuid) override
    { ++calls; lastUuid = uuid; return answer; }
    pet::PluginError setGraphicsBackend(const QString& uuid,
                                        const QString& backend) override
    { ++calls; lastUuid = uuid; lastPackId = backend; return answer; }
};

// Programmable fake lifecycle API (same shape as RosterApiModelTest's).
class FakeControlApi : public pet::IInstanceControlApi
{
public:
    pet::PluginError answer = pet::PluginError::Ok;
    QString lastUuid, lastModel;
    QString lastOp;
    int calls = 0;

    pet::PluginError create(const pet::InstanceSpec&, QString*) override
    { return pet::PluginError::Generic; } // not part of the bridge surface
    pet::PluginError remove(const QString&) override
    { return pet::PluginError::Generic; } // not part of the bridge surface
    pet::PluginError start(const QString& uuid) override
    { ++calls; lastOp = QStringLiteral("start"); lastUuid = uuid; return answer; }
    pet::PluginError stop(const QString& uuid) override
    { ++calls; lastOp = QStringLiteral("stop"); lastUuid = uuid; return answer; }
    pet::PluginError restart(const QString& uuid) override
    { ++calls; lastOp = QStringLiteral("restart"); lastUuid = uuid; return answer; }
    pet::PluginError loadModel(const QString& uuid, const QString& modelName) override
    { ++calls; lastOp = QStringLiteral("loadModel"); lastUuid = uuid;
      lastModel = modelName; return answer; }
};

// Programmable fake layout API (v1.8): records the last call + answers
// with the configured error; getLayout answers a fixture LayoutInfo.
class FakeLayoutApi : public pet::ILayoutApi
{
public:
    pet::PluginError answer = pet::PluginError::Ok;
    pet::LayoutInfo layout;
    QString lastUuid;
    bool lastAutoStart = false;
    int setAutoStartCalls = 0;
    int resetLayoutCalls = 0;
    int getLayoutCalls = 0;

    pet::PluginError getLayout(const QString& uuid,
                               pet::LayoutInfo* out) override
    {
        ++getLayoutCalls;
        lastUuid = uuid;
        if (out != nullptr)
            *out = answer == pet::PluginError::Ok ? layout
                                                  : pet::LayoutInfo{};
        return answer;
    }
    pet::PluginError resetLayout(const QString& uuid) override
    { ++resetLayoutCalls; lastUuid = uuid; return answer; }
    pet::PluginError setAutoStart(const QString& uuid, bool enabled) override
    { ++setAutoStartCalls; lastUuid = uuid; lastAutoStart = enabled;
      return answer; }
};

} // namespace

class InstanceControlBridgeTest : public QObject
{
    Q_OBJECT

private slots:
    void testForwardsToTuningApi();
    void testForwardsToControlApi();
    void testForwardsToLayoutApi();
    void testNullApisReturnGeneric();
};

void InstanceControlBridgeTest::testForwardsToTuningApi()
{
    FakeTuningApi tuning;
    FakeControlApi control;
    FakeLayoutApi layout;
    InstanceControlBridge bridge(&tuning, &control, &layout);
    const QString uuid = QStringLiteral("uuid-1");

    QCOMPARE(bridge.setOpacity(uuid, 0.75),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.calls, 1);
    QCOMPARE(tuning.lastUuid, uuid);
    QCOMPARE(tuning.lastDouble, 0.75);

    QCOMPARE(bridge.setVolume(uuid, 0.25), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastDouble, 0.25);
    QCOMPARE(bridge.setMuted(uuid, true), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastBool, true);
    QCOMPARE(bridge.setFps(uuid, 60), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastInt, 60);
    QCOMPARE(bridge.playMotion(uuid, QStringLiteral("Idle"), 3),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastGroup, QStringLiteral("Idle"));
    QCOMPARE(tuning.lastInt, 3);
    QCOMPARE(bridge.setExpression(uuid, QStringLiteral("smile")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastExpr, QStringLiteral("smile"));
    QCOMPARE(bridge.triggerHitArea(uuid, QStringLiteral("Head")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastArea, QStringLiteral("Head"));
    QCOMPARE(bridge.mountVoicePack(uuid, QStringLiteral("pack_v1")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.lastPackId, QStringLiteral("pack_v1"));
    QCOMPARE(bridge.unmountVoicePack(uuid),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(tuning.calls, 9);

    // Error codes pass through verbatim (Busy = 5 — the pending-delete
    // case the detail page surfaces).
    tuning.answer = pet::PluginError::Busy;
    QCOMPARE(bridge.setOpacity(uuid, 0.5), static_cast<int>(pet::PluginError::Busy));
    tuning.answer = pet::PluginError::NotFound;
    QCOMPARE(bridge.mountVoicePack(uuid, QStringLiteral("gone")),
             static_cast<int>(pet::PluginError::NotFound));
}

void InstanceControlBridgeTest::testForwardsToControlApi()
{
    FakeTuningApi tuning;
    FakeControlApi control;
    FakeLayoutApi layout;
    InstanceControlBridge bridge(&tuning, &control, &layout);
    const QString uuid = QStringLiteral("uuid-2");

    QCOMPARE(bridge.start(uuid), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastOp, QStringLiteral("start"));
    QCOMPARE(bridge.stop(uuid), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastOp, QStringLiteral("stop"));
    QCOMPARE(bridge.restart(uuid), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastOp, QStringLiteral("restart"));
    QCOMPARE(bridge.loadModel(uuid, QStringLiteral("Haru")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastOp, QStringLiteral("loadModel"));
    QCOMPARE(control.lastModel, QStringLiteral("Haru"));
    QCOMPARE(control.lastUuid, uuid);
    QCOMPARE(control.calls, 4);

    control.answer = pet::PluginError::Busy;
    QCOMPARE(bridge.restart(uuid), static_cast<int>(pet::PluginError::Busy));
}

void InstanceControlBridgeTest::testForwardsToLayoutApi()
{
    FakeTuningApi tuning;
    FakeControlApi control;
    FakeLayoutApi layout;
    InstanceControlBridge bridge(&tuning, &control, &layout);
    const QString uuid = QStringLiteral("uuid-4");

    // setAutoStart forwards (uuid + flag verbatim).
    QCOMPARE(bridge.setAutoStart(uuid, true),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(layout.setAutoStartCalls, 1);
    QCOMPARE(layout.lastUuid, uuid);
    QVERIFY(layout.lastAutoStart);
    QCOMPARE(bridge.setAutoStart(uuid, false),
             static_cast<int>(pet::PluginError::Ok));
    QVERIFY(!layout.lastAutoStart);

    // resetLayout forwards.
    QCOMPARE(bridge.resetLayout(uuid), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(layout.resetLayoutCalls, 1);
    QCOMPARE(layout.lastUuid, uuid);

    // getLayout: the success map shape {ok,x,y,width,height,scale}.
    layout.layout.x = 1200.0;
    layout.layout.y = 600.0;
    layout.layout.width = 400.0;
    layout.layout.height = 500.0;
    layout.layout.scale = 1.75;
    const QVariantMap okMap = bridge.getLayout(uuid);
    QCOMPARE(layout.getLayoutCalls, 1);
    QCOMPARE(okMap.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(okMap.value(QStringLiteral("x")).toDouble(), 1200.0);
    QCOMPARE(okMap.value(QStringLiteral("y")).toDouble(), 600.0);
    QCOMPARE(okMap.value(QStringLiteral("width")).toDouble(), 400.0);
    QCOMPARE(okMap.value(QStringLiteral("height")).toDouble(), 500.0);
    QCOMPARE(okMap.value(QStringLiteral("scale")).toDouble(), 1.75);
    QCOMPARE(okMap.size(), 6);

    // Error codes pass through as the {ok:false,error} shape
    // (NotFound = 4 — the unknown-uuid case; Busy = 5 pending-delete).
    layout.answer = pet::PluginError::NotFound;
    const QVariantMap nfMap = bridge.getLayout(uuid);
    QCOMPARE(nfMap.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(nfMap.value(QStringLiteral("error")).toInt(),
             static_cast<int>(pet::PluginError::NotFound));
    QCOMPARE(nfMap.size(), 2);
    layout.answer = pet::PluginError::Busy;
    QCOMPARE(bridge.resetLayout(uuid), static_cast<int>(pet::PluginError::Busy));
    QCOMPARE(bridge.setAutoStart(uuid, true),
             static_cast<int>(pet::PluginError::Busy));
}

void InstanceControlBridgeTest::testNullApisReturnGeneric()
{
    InstanceControlBridge bridge(nullptr, nullptr, nullptr);
    const QString uuid = QStringLiteral("uuid-3");

    QCOMPARE(bridge.setOpacity(uuid, 0.5),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.setVolume(uuid, 0.5),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.setMuted(uuid, true),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.setFps(uuid, 30),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.playMotion(uuid, QStringLiteral("Idle"), 0),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.setExpression(uuid, QStringLiteral("e")),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.triggerHitArea(uuid, QStringLiteral("Head")),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.mountVoicePack(uuid, QStringLiteral("p")),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.unmountVoicePack(uuid),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.start(uuid), static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.stop(uuid), static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.restart(uuid), static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.loadModel(uuid, QStringLiteral("M")),
             static_cast<int>(pet::PluginError::Generic));
    // v1.8 layout surface: null layout API → Generic ints + the failure
    // map shape, never a crash.
    QCOMPARE(bridge.setAutoStart(uuid, true),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(bridge.resetLayout(uuid),
             static_cast<int>(pet::PluginError::Generic));
    const QVariantMap map = bridge.getLayout(uuid);
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("error")).toInt(),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(map.size(), 2);
}

QTEST_APPLESS_MAIN(InstanceControlBridgeTest)
#include "InstanceControlBridgeTest.moc"
