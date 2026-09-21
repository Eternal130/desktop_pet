// RosterApiModelTest (S3 roster dogfooding): the sidebar roster READ model
// over pet::IInstanceApi. Locks the contract with a programmable fake API
// (the interface is pure virtual — no core/ instance needed):
//   - testInitialLoad: constructor pulls the instances() snapshot; rows,
//     count and every role land before the first QML binding.
//   - testRosterChangedResets: a rosterChanged fanout rebuilds via full
//     model reset (QSignalSpy on modelReset) and countChanged fires only
//     when the size actually changed.
//   - testRoleDataAndNames: roleNames bytes match InstanceManager's roster
//     roles (label/modelName/status/connected/uuid — QML delegates bind
//     unchanged), data() per role, out-of-range row and invalid-parent
//     safety.
//   - testUnsubscribesOnDestruction: the destructor removes the observer
//     from the API (observerCount()==0) and a later fanout is a no-op —
//     no crash, no zombie refresh.
//   - testNullApiTolerated: null API → empty inert model (degraded wiring
//     never crashes).
//   - S2 write bridge: testCreateForwardsToControlApi /
//     testDeleteForwardsToControlApi (Q_INVOKABLE → control-API forward,
//     spec passthrough, error-code passthrough) /
//     testNullControlApiReturnsGeneric (degraded wiring: loud Generic,
//     read side unaffected).
//
// Links pet_panel_core (RosterApiModel lives there). QTEST_APPLESS_MAIN —
// pure model + synchronous fake fanout, no event loop needed.
#include <QSignalSpy>
#include <QTest>

#include "api/IInstanceApi.hpp"
#include "ui/RosterApiModel.hpp"

namespace {

pet::InstanceInfo info(const QString& uuid, const QString& label,
                       const QString& modelName, const QString& status,
                       bool connected)
{
    pet::InstanceInfo i;
    i.uuid = uuid;
    i.label = label;
    i.modelName = modelName;
    i.status = status;
    i.connected = connected;
    return i;
}

// Programmable fake: preset snapshot + manual rosterChanged fanout +
// observer bookkeeping so the unsubscribe contract is assertable.
// v1.2 note: the two subscribeInstances/unsubscribeInstances overrides
// are required since the IInstanceApi tail append (S2) — no-op bodies,
// the read-model tests never exercise the instance-observer family.
class FakeInstanceApi : public pet::IInstanceApi
{
public:
    QVector<pet::InstanceInfo> instances() override { return m_rows; }
    void subscribeRoster(pet::IRosterObserver* observer) override
    {
        if (observer != nullptr && !m_observers.contains(observer))
            m_observers.append(observer);
    }
    void unsubscribeRoster(pet::IRosterObserver* observer) override
    {
        m_observers.removeAll(observer);
    }
    void subscribeInstances(pet::IInstanceObserver*) override {}
    void unsubscribeInstances(pet::IInstanceObserver*) override {}

    void setRows(QVector<pet::InstanceInfo> rows) { m_rows = std::move(rows); }
    void emitRosterChanged()
    {
        const auto observers = m_observers; // copy — unsubscribe-safe fanout
        for (pet::IRosterObserver* o : observers)
            o->rosterChanged();
    }
    int observerCount() const { return m_observers.size(); }

private:
    QVector<pet::InstanceInfo> m_rows;
    QVector<pet::IRosterObserver*> m_observers;
};

// S2 write-bridge fake: records the forwarded arguments + returns a
// programmable error code so passthrough is assertable.
class FakeControlApi : public pet::IInstanceControlApi
{
public:
    pet::PluginError create(const pet::InstanceSpec& spec, QString* outUuid) override
    {
        lastSpec = spec;
        if (outUuid != nullptr)
            *outUuid = QStringLiteral("fake-uuid");
        return createResult;
    }
    pet::PluginError remove(const QString& uuid) override
    {
        lastRemovedUuid = uuid;
        return removeResult;
    }
    pet::PluginError start(const QString&) override { return pet::PluginError::Ok; }
    pet::PluginError stop(const QString&) override { return pet::PluginError::Ok; }
    pet::PluginError restart(const QString&) override { return pet::PluginError::Ok; }
    pet::PluginError loadModel(const QString&, const QString&) override
    {
        return pet::PluginError::Ok;
    }

    pet::InstanceSpec lastSpec;
    QString lastRemovedUuid;
    pet::PluginError createResult = pet::PluginError::Ok;
    pet::PluginError removeResult = pet::PluginError::Ok;
};

} // namespace

class RosterApiModelTest : public QObject
{
    Q_OBJECT

private slots:
    void testInitialLoad();
    void testRosterChangedResets();
    void testRoleDataAndNames();
    void testUnsubscribesOnDestruction();
    void testNullApiTolerated();

    // ── S2 write bridge (Q_INVOKABLE → pet::IInstanceControlApi forward) ──
    void testCreateForwardsToControlApi();
    void testDeleteForwardsToControlApi();
    void testNullControlApiReturnsGeneric();
};

void RosterApiModelTest::testInitialLoad()
{
    FakeInstanceApi api;
    api.setRows({
        info("uuid-a", QStringLiteral("Pet A"), QStringLiteral("Hiyori"),
             QStringLiteral("running"), true),
        info("uuid-b", QStringLiteral("Pet B"), QStringLiteral("Haru"),
             QStringLiteral("stopped"), false),
    });

    RosterApiModel model(&api);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.count(), 2);
    // ctor subscribed exactly once
    QCOMPARE(api.observerCount(), 1);
    // first row's identity fields
    const QModelIndex first = model.index(0, 0);
    QCOMPARE(model.data(first, RosterApiModel::UuidRole).toString(),
             QStringLiteral("uuid-a"));
    QCOMPARE(model.data(first, RosterApiModel::LabelRole).toString(),
             QStringLiteral("Pet A"));
}

void RosterApiModelTest::testRosterChangedResets()
{
    FakeInstanceApi api;
    api.setRows({info("uuid-a", "A", "Hiyori", "running", true)});
    RosterApiModel model(&api);

    QSignalSpy resetSpy(&model, &QAbstractListModel::modelReset);
    QSignalSpy countSpy(&model, &RosterApiModel::countChanged);

    // Same size, different content → reset fires, countChanged does NOT.
    api.setRows({info("uuid-z", "Z", "Haru", "stopped", false)});
    api.emitRosterChanged();
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(countSpy.count(), 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), RosterApiModel::UuidRole).toString(),
             QStringLiteral("uuid-z"));

    // Size change → countChanged fires alongside the reset.
    api.setRows({info("uuid-z", "Z", "Haru", "stopped", false),
                 info("uuid-y", "Y", "Mao", "running", true),
                 info("uuid-x", "X", "Rice", "running", true)});
    api.emitRosterChanged();
    QCOMPARE(resetSpy.count(), 2);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.count(), 3);

    // Empty roster is representable.
    api.setRows({});
    api.emitRosterChanged();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
    QCOMPARE(countSpy.count(), 2);
}

void RosterApiModelTest::testRoleDataAndNames()
{
    FakeInstanceApi api;
    api.setRows({info("uuid-1", QStringLiteral("喵喵"),
                      QStringLiteral("Hiyori"), QStringLiteral("running"),
                      true)});
    RosterApiModel model(&api);

    // Role names byte-match InstanceManager's roster roles (minus "avatar",
    // which pet::InstanceInfo does not carry) — that is what lets the
    // sidebar delegate bind unchanged.
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(RosterApiModel::UuidRole), QByteArrayLiteral("uuid"));
    QCOMPARE(names.value(RosterApiModel::LabelRole), QByteArrayLiteral("label"));
    QCOMPARE(names.value(RosterApiModel::ModelNameRole),
             QByteArrayLiteral("modelName"));
    QCOMPARE(names.value(RosterApiModel::StatusRole),
             QByteArrayLiteral("status"));
    QCOMPARE(names.value(RosterApiModel::ConnectedRole),
             QByteArrayLiteral("connected"));
    QCOMPARE(names.size(), 5);

    const QModelIndex row = model.index(0, 0);
    QCOMPARE(model.data(row, RosterApiModel::LabelRole).toString(),
             QStringLiteral("喵喵"));
    QCOMPARE(model.data(row, RosterApiModel::ModelNameRole).toString(),
             QStringLiteral("Hiyori"));
    QCOMPARE(model.data(row, RosterApiModel::StatusRole).toString(),
             QStringLiteral("running"));
    QCOMPARE(model.data(row, RosterApiModel::ConnectedRole).toBool(), true);

    // Out-of-range row / unknown role → invalid QVariant, never a crash.
    QVERIFY(!model.data(model.index(5, 0), RosterApiModel::LabelRole).isValid());
    QVERIFY(!model.data(row, Qt::UserRole + 99).isValid());
    // Flat list: any valid parent index yields zero children.
    QCOMPARE(model.rowCount(QModelIndex()), model.count());
}

void RosterApiModelTest::testUnsubscribesOnDestruction()
{
    FakeInstanceApi api;
    api.setRows({info("uuid-a", "A", "Hiyori", "running", true)});

    {
        RosterApiModel model(&api);
        QCOMPARE(api.observerCount(), 1);
    } // scope exit → dtor must unsubscribe

    QCOMPARE(api.observerCount(), 0);
    // Fanout after the model is gone: no observers, nothing to refresh,
    // and (crucially) no crash.
    api.setRows({});
    api.emitRosterChanged();
    QCOMPARE(api.observerCount(), 0);
}

void RosterApiModelTest::testNullApiTolerated()
{
    RosterApiModel model(nullptr); // degraded wiring — must not crash
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.count(), 0);
    QVERIFY(!model.data(model.index(0, 0), RosterApiModel::LabelRole).isValid());
}

// ────────────────────────────────────────────────────────────────────────────
// S2 write bridge: the Q_INVOKABLEs forward into the injected control API
// verbatim and surface the PluginError code as a plain int (0 = Ok).
// ────────────────────────────────────────────────────────────────────────────

void RosterApiModelTest::testCreateForwardsToControlApi()
{
    FakeInstanceApi api;
    FakeControlApi control;
    RosterApiModel model(&api, &control);

    QCOMPARE(model.createInstance(QStringLiteral("喵喵"),
                                  QStringLiteral("🐶"),
                                  QStringLiteral("Haru"), true),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastSpec.label, QStringLiteral("喵喵"));
    QCOMPARE(control.lastSpec.avatar, QStringLiteral("🐶"));
    QCOMPARE(control.lastSpec.modelName, QStringLiteral("Haru"));
    QVERIFY(control.lastSpec.autoStart);

    // Defaults keep the QML call sites (label, avatar) working.
    QCOMPARE(model.createInstance(QStringLiteral("Solo")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastSpec.avatar, QStringLiteral("🐱"));
    QVERIFY(control.lastSpec.modelName.isEmpty());
    QVERIFY(!control.lastSpec.autoStart);

    // Error-code passthrough (e.g. InvalidArgument from an empty label).
    control.createResult = pet::PluginError::InvalidArgument;
    QCOMPARE(model.createInstance(QStringLiteral("x")),
             static_cast<int>(pet::PluginError::InvalidArgument));
}

void RosterApiModelTest::testDeleteForwardsToControlApi()
{
    FakeInstanceApi api;
    FakeControlApi control;
    RosterApiModel model(&api, &control);

    QCOMPARE(model.deleteInstance(QStringLiteral("uuid-1")),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(control.lastRemovedUuid, QStringLiteral("uuid-1"));

    control.removeResult = pet::PluginError::NotFound;
    QCOMPARE(model.deleteInstance(QStringLiteral("gone")),
             static_cast<int>(pet::PluginError::NotFound));
}

void RosterApiModelTest::testNullControlApiReturnsGeneric()
{
    FakeInstanceApi api;
    RosterApiModel model(&api, nullptr); // degraded wiring — loud, no crash
    QCOMPARE(model.createInstance(QStringLiteral("L")),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(model.deleteInstance(QStringLiteral("uuid")),
             static_cast<int>(pet::PluginError::Generic));
    QCOMPARE(model.rowCount(), 0); // read side unaffected
}

QTEST_APPLESS_MAIN(RosterApiModelTest)
#include "RosterApiModelTest.moc"
