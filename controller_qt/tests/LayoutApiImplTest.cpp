// LayoutApiImplTest (v1.8) — the real pet::ILayoutApi implementation over
// a REAL InstanceManager harness (same QTemporaryDir + WsServer +
// PendingRequests + savePanel-lambda pattern as TuningApiImplTest) plus
// the queryApi routing matrix (the PluginQueryApiTest shape, kept in
// THIS binary so the family's whole contract lives in one place — the
// task's "二选一" resolved in favor of a self-contained family test).
// Locks:
//   1. testGetLayoutProjectsConfigSnapshot: a PRE-SEEDED instance
//      config (distinctive window geometry + layout scale, loaded from
//      disk by the manager ctor) projects field-for-field into
//      LayoutInfo (x=windowX / y=windowY / width=windowWidth /
//      height=windowHeight / scale=layoutScale — NOT modelScale); a
//      default-created instance projects the InstanceConfig defaults;
//      null out → InvalidArgument (never a half-written POD).
//   2. testWritesForward: setAutoStart persists (live session flag +
//      the on-disk config file); resetLayout forwards to the session's
//      fire-and-forget reset_layout command (commandSent captures the
//      action); both return Ok.
//   3. testUnknownUuidNotFound: every op on a uuid not in the roster →
//      NotFound (incl. the null-manager degraded-wiring case).
//   4. testPendingDeleteIsBusy: after setDeletePending (the public
//      two-phase mark), every op returns Busy.
//   5. testQueryApiRoutingAndStub: the write-family matrix for
//      "pet.layout" — granted + switch on + injected → the SHARED real
//      impl (pointer identity); not granted / switch off / not injected
//      → capability stub (every method Capability, *out zeroed);
//      minVersion above the family version → nullptr.
//
// QTEST_MAIN — InstanceManager harness parity (WsServer machinery is
// constructed; no async stop is driven here).
// Links pet_panel_core (LayoutApiImpl + PluginContextImpl live there).

#include "api/ILayoutApi.hpp"
#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <memory>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// The shared harness trio (TuningApiImplTest shape).
struct Harness
{
    QTemporaryDir base;
    WsServer server;
    PendingRequests pending;
    std::unique_ptr<InstanceManager> mgr;

    explicit Harness(const QString& label = QString())
        : mgr(nullptr)
    {
        QVERIFY2(base.isValid(), "temp dir");
        mgr = std::make_unique<InstanceManager>(
            base.path(), server, pending,
            [this](const PanelConfig& cfg) {
                PanelStateManager(base.path()).save(cfg);
            });
        if (!label.isEmpty()) {
            const QString uuid = mgr->createInstance(label);
            QVERIFY2(!uuid.isEmpty(), "createInstance failed");
        }
    }
};

} // namespace

class LayoutApiImplTest : public QObject
{
    Q_OBJECT

private slots:
    void testGetLayoutProjectsConfigSnapshot();
    void testWritesForward();
    void testUnknownUuidNotFound();
    void testPendingDeleteIsBusy();
    void testQueryApiRoutingAndStub();
};

// ── 1. getLayout projects the persisted config snapshot ─────────────────────

void LayoutApiImplTest::testGetLayoutProjectsConfigSnapshot()
{
    // Pre-seed a config + panel.json instanceIds so the manager ctor
    // loads it from disk with DISTINCTIVE values (m4 load order).
    QTemporaryDir base;
    QVERIFY(base.isValid());
    InstanceConfig cfg;
    cfg.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
    cfg.label = QStringLiteral("Seeded");
    cfg.windowX = 111;
    cfg.windowY = 222;
    cfg.windowWidth = 333;
    cfg.windowHeight = 444;
    cfg.layoutScale = 1.75; // the set_layout domain — what LayoutInfo.scale mirrors
    cfg.modelScale = 9.5;   // the LEGACY boot field — must NOT leak into scale
    QVERIFY(InstanceConfigManager(base.path()).save(cfg));
    PanelConfig panel;
    panel.instanceIds.append(cfg.id);
    QVERIFY(PanelStateManager(base.path()).save(panel));

    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& pc) {
                            PanelStateManager(base.path()).save(pc);
                        });
    QCOMPARE(mgr.rowCount(), 1);
    core::LayoutApiImpl api(&mgr, nullptr);

    pet::LayoutInfo info;
    QCOMPARE(api.getLayout(cfg.id, &info), pet::PluginError::Ok);
    QCOMPARE(info.x, 111.0);
    QCOMPARE(info.y, 222.0);
    QCOMPARE(info.width, 333.0);
    QCOMPARE(info.height, 444.0);
    QCOMPARE(info.scale, 1.75); // layoutScale, NOT modelScale (9.5)

    // Default-created instance → the InstanceConfig defaults.
    const QString uuid = mgr.createInstance(QStringLiteral("Default"));
    QVERIFY(!uuid.isEmpty());
    pet::LayoutInfo def;
    QCOMPARE(api.getLayout(uuid, &def), pet::PluginError::Ok);
    QCOMPARE(def.x, 1200.0);
    QCOMPARE(def.y, 600.0);
    QCOMPARE(def.width, 400.0);
    QCOMPARE(def.height, 500.0);
    QCOMPARE(def.scale, 1.0);

    // Null out pointer → InvalidArgument (the header contract).
    QCOMPARE(api.getLayout(uuid, nullptr), pet::PluginError::InvalidArgument);

    // A miss leaves *out ZERO-initialized, never half-written.
    pet::LayoutInfo stale;
    stale.x = 77.0;
    QCOMPARE(api.getLayout(QStringLiteral("nope"), &stale),
             pet::PluginError::NotFound);
    QCOMPARE(stale.x, 0.0);
}

// ── 2. resetLayout / setAutoStart forward to the session surfaces ───────────

void LayoutApiImplTest::testWritesForward()
{
    Harness h(QStringLiteral("Write"));
    core::LayoutApiImpl api(h.mgr.get(), nullptr);
    InstanceSession* session = h.mgr->instanceAt(0);
    QVERIFY(session != nullptr);
    const QString uuid = session->uuid();
    QVERIFY(!session->autoStartEnabled());

    // setAutoStart persists: live session flag + the on-disk config file
    // (the same InstanceSession::setAutoStart the detail page drove).
    QSignalSpy commands(session, &InstanceSession::commandSent);
    QCOMPARE(api.setAutoStart(uuid, true), pet::PluginError::Ok);
    QVERIFY(session->autoStartEnabled());
    const auto persisted = InstanceConfigManager(h.base.path()).load(uuid);
    QVERIFY(persisted.has_value());
    QVERIFY(persisted->autoStart);

    // setAutoStart sends NO command (the renderer is not up at toggle
    // time — persist-only semantics).
    QCOMPARE(commands.count(), 0);

    // resetLayout: accepted → the session's fire-and-forget
    // reset_layout command hits the command stream.
    QCOMPARE(api.resetLayout(uuid), pet::PluginError::Ok);
    QCOMPARE(commands.count(), 1);
    QCOMPARE(commands.at(0).at(0).toString(), QStringLiteral("reset_layout"));

    // Idempotent re-set of the same autoStart value stays Ok.
    QCOMPARE(api.setAutoStart(uuid, true), pet::PluginError::Ok);
}

// ── 3. unknown uuid → NotFound (incl. degraded wiring) ──────────────────────

void LayoutApiImplTest::testUnknownUuidNotFound()
{
    Harness h(QStringLiteral("Alive"));
    core::LayoutApiImpl api(h.mgr.get(), nullptr);
    const QString gone = QStringLiteral("no-such-uuid");

    pet::LayoutInfo info;
    info.x = 5.0;
    QCOMPARE(api.getLayout(gone, &info), pet::PluginError::NotFound);
    QCOMPARE(info.x, 0.0); // zeroed on the miss
    QCOMPARE(api.resetLayout(gone), pet::PluginError::NotFound);
    QCOMPARE(api.setAutoStart(gone, true), pet::PluginError::NotFound);

    // Degraded wiring: null InstanceManager → NotFound everywhere (never
    // a crash).
    core::LayoutApiImpl degraded(nullptr, nullptr);
    QCOMPARE(degraded.getLayout(gone, &info), pet::PluginError::NotFound);
    QCOMPARE(degraded.resetLayout(gone), pet::PluginError::NotFound);
    QCOMPARE(degraded.setAutoStart(gone, true), pet::PluginError::NotFound);
}

// ── 4. pending-delete → Busy ────────────────────────────────────────────────

void LayoutApiImplTest::testPendingDeleteIsBusy()
{
    Harness h(QStringLiteral("Doomed"));
    core::LayoutApiImpl api(h.mgr.get(), nullptr);
    InstanceSession* session = h.mgr->instanceAt(0);
    QVERIFY(session != nullptr);
    // The exact mark InstanceManager::deleteInstance's two-phase path
    // sets before initiating the async stop (public seam; the
    // TuningApiImplTest::locateAlive precedent).
    session->setDeletePending();

    pet::LayoutInfo info;
    QCOMPARE(api.getLayout(session->uuid(), &info), pet::PluginError::Busy);
    QCOMPARE(api.resetLayout(session->uuid()), pet::PluginError::Busy);
    QCOMPARE(api.setAutoStart(session->uuid(), true), pet::PluginError::Busy);
}

// ── 5. queryApi routing matrix + the stub ───────────────────────────────────

void LayoutApiImplTest::testQueryApiRoutingAndStub()
{
    Harness h(QStringLiteral("Route"));
    core::InstanceApiImpl rosterApi(h.mgr.get(), nullptr);
    core::InstanceControlApiImpl sharedControl(h.mgr.get(), nullptr);
    core::LayoutApiImpl sharedLayout(h.mgr.get(), nullptr);

    bool writeSwitch = true;
    const QStringList granted{QStringLiteral("instance_tuning")};
    const QStringList notGranted{QStringLiteral("network")};

    auto makeContext = [&](const QStringList& caps) {
        auto* ctx = new core::PluginContextImpl(
            QStringLiteral("org.test.layout"), &rosterApi, &sharedControl,
            [&writeSwitch]() { return writeSwitch; },
            /*pageModel=*/nullptr, /*stream=*/nullptr, h.base.path(),
            /*downloadService=*/nullptr, caps,
            /*voicePackRefresh=*/{}, /*parent=*/nullptr);
        ctx->setLayoutApi(&sharedLayout);
        return ctx;
    };

    // 1) granted + switch on + injected → the SHARED real implementation
    //    (pointer identity) — reads work through it.
    {
        auto* ctx = makeContext(granted);
        pet::IExtApi* api = ctx->queryApi(pet::kLayoutApiId, 1);
        QVERIFY(api != nullptr);
        QCOMPARE(api, static_cast<pet::IExtApi*>(&sharedLayout));
        auto* layout = dynamic_cast<pet::ILayoutApi*>(api);
        QVERIFY(layout != nullptr);
        const QString uuid = h.mgr->instanceAt(0)->uuid();
        pet::LayoutInfo info;
        QCOMPARE(layout->getLayout(uuid, &info), pet::PluginError::Ok);
        QCOMPARE(info.scale, 1.0); // default-created instance
        QCOMPARE(layout->setAutoStart(uuid, true), pet::PluginError::Ok);
        delete ctx;
    }

    // 2) granted + switch OFF → capability stub.
    {
        writeSwitch = false;
        auto* ctx = makeContext(granted);
        pet::IExtApi* api = ctx->queryApi(pet::kLayoutApiId, 1);
        QVERIFY(api != nullptr);
        QVERIFY(api != static_cast<pet::IExtApi*>(&sharedLayout));
        auto* layout = dynamic_cast<pet::ILayoutApi*>(api);
        QVERIFY(layout != nullptr);
        pet::LayoutInfo info;
        info.x = 9.0;
        QCOMPARE(layout->getLayout(QStringLiteral("any"), &info),
                 pet::PluginError::Capability);
        QCOMPARE(info.x, 0.0); // zeroed by the stub
        QCOMPARE(layout->resetLayout(QStringLiteral("any")),
                 pet::PluginError::Capability);
        QCOMPARE(layout->setAutoStart(QStringLiteral("any"), true),
                 pet::PluginError::Capability);
        delete ctx;
        writeSwitch = true;
    }

    // 3) NOT granted (switch on) → capability stub.
    {
        auto* ctx = makeContext(notGranted);
        pet::IExtApi* api = ctx->queryApi(pet::kLayoutApiId, 1);
        QVERIFY(api != nullptr);
        QVERIFY(api != static_cast<pet::IExtApi*>(&sharedLayout));
        auto* layout = dynamic_cast<pet::ILayoutApi*>(api);
        QVERIFY(layout != nullptr);
        QCOMPARE(layout->resetLayout(QStringLiteral("any")),
                 pet::PluginError::Capability);
        delete ctx;
    }

    // 4) NOT injected (degraded wiring — the write-family matrix keeps a
    //    stub, not a nullptr) → stub, never a crash.
    {
        auto* ctx = new core::PluginContextImpl(
            QStringLiteral("org.test.unwired"), &rosterApi, &sharedControl,
            []() { return true; },
            nullptr, nullptr, h.base.path(), nullptr, granted, {}, nullptr);
        pet::IExtApi* api = ctx->queryApi(pet::kLayoutApiId, 1);
        QVERIFY(api != nullptr);
        auto* layout = dynamic_cast<pet::ILayoutApi*>(api);
        QVERIFY(layout != nullptr);
        QCOMPARE(layout->resetLayout(QStringLiteral("any")),
                 pet::PluginError::Capability);
        delete ctx;
    }

    // 5) minVersion above the family version → nullptr ("feature
    //    absent"); the exact version is served.
    {
        auto* ctx = makeContext(granted);
        QCOMPARE(ctx->queryApi(pet::kLayoutApiId,
                               pet::kLayoutApiVersion + 1),
                 nullptr);
        QVERIFY(ctx->queryApi(pet::kLayoutApiId,
                              pet::kLayoutApiVersion) != nullptr);
        QCOMPARE(ctx->queryApi(nullptr, 1), nullptr);
        delete ctx;
    }
}

QTEST_MAIN(LayoutApiImplTest)
#include "LayoutApiImplTest.moc"
