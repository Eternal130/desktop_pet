// PluginQueryApiTest (S2, v1.2) — the queryApi gating matrix + the
// bidirectional plugin↔UI dogfooding case.
//
// Gating matrix (PluginContextImpl::queryApi for "pet.instance_control"):
//   - capability granted + write switch on   → the SHARED real
//     InstanceControlApiImpl (pointer identity)
//   - capability granted + write switch off  → capability stub (every
//     method returns PluginError::Capability)
//   - capability NOT granted                 → capability stub
//   - unknown apiId / null apiId / minVersion above the family version →
//     nullptr ("feature absent")
//   - degraded wiring (null shared impl)     → stub
//
// Bidirectional dogfooding (fake static plugin through a real PluginHost
// over a real InstanceManager harness, same pattern as PluginHostTest +
// InstanceManagerTest):
//   - plugin → host: the fake plugin's initialize() queryApi's the
//     control API and CREATES an instance; the roster side (shared
//     pet::IInstanceApi) sees it, and the panel's own RosterApiModel
//     (UI write bridge) reflects it.
//   - host → plugin: RosterApiModel::createInstance (the UI write path)
//     fires rosterChanged into an observer the plugin registered in
//     initialize().
//
// QTEST_MAIN — PluginHost's plugin lifecycle uses plain QObject machinery;
// the event loop is available for symmetry with PluginHostTest.
// Links pet_panel_core (PluginContextImpl / PluginHost / RosterApiModel).

#include "api/IPanelPlugin.hpp"
#include "api/IInstanceControlApi.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceManager.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "core/PluginContextImpl.hpp"
#include "core/PluginHost.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"
#include "ui/RosterApiModel.hpp"

#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

using core::PluginHost;
using core::PluginRegistry;

// ── Fake plugin (file-static record vectors — factories are plain
// function pointers, no capturing state possible; same harness shape as
// PluginHostTest).
struct FakeLifecyclePlugin;
FakeLifecyclePlugin* g_lastPlugin = nullptr;

// Observer the plugin registers in initialize() — the host→plugin
// direction of the dogfooding case.
struct PluginSideObserver : public pet::IRosterObserver
{
    int rosterChanges = 0;
    void rosterChanged() override { ++rosterChanges; }
};

struct FakeLifecyclePlugin final : public pet::IPanelPlugin
{
    pet::IPluginContext* ctx = nullptr;
    PluginSideObserver observer;
    pet::IExtApi* queried = nullptr;
    pet::PluginError createResult = pet::PluginError::Generic;
    QString createdUuid;

    pet::PluginError initialize(pet::IPluginContext& context) override
    {
        ctx = &context;
        // Subscribe FIRST so the UI-side create below lands on this
        // observer.
        context.instanceApi().subscribeRoster(&observer);
        queried = context.queryApi(pet::kInstanceControlApiId, 1);
        if (queried != nullptr) {
            auto* control = dynamic_cast<pet::IInstanceControlApi*>(queried);
            if (control != nullptr) {
                pet::InstanceSpec spec;
                spec.label = QStringLiteral("PluginPet");
                spec.modelName = QStringLiteral("Haru");
                createResult = control->create(spec, &createdUuid);
            }
        }
        return pet::PluginError::Ok;
    }

    void shutdown() override
    {
        if (ctx != nullptr)
            ctx->instanceApi().unsubscribeRoster(&observer);
    }
};

pet::IPanelPlugin* spawnLifecyclePlugin()
{
    auto* p = new FakeLifecyclePlugin;
    g_lastPlugin = p;
    return p;
}

QString manifestWithCapabilities(const char* id, const char* capabilities)
{
    // NOTE: plain escaped-quote literal, NOT a multi-line R"json(...)json"
    // raw string — moc's preprocessor mis-lexes multi-line raw string
    // literals and then fails to find the Q_OBJECT class below (empty .moc
    // → undefined vtable at link time; same family as the known moc
    // std::function pitfall in InstanceSession.hpp).
    return QStringLiteral(
        "{\"id\":\"%1\",\"version\":\"1.0\",\"api_version\":\"1.2\","
        "\"order\":100,\"capabilities\":[%2]}")
        .arg(QLatin1String(id), QLatin1String(capabilities));
}

} // namespace

class PluginQueryApiTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { g_lastPlugin = nullptr; }
    void cleanup()
    {
        // Host-owned plugins are deleted in shutdownAll; only a leftover
        // from a failed init would remain (none in these slots).
    }

    void testGatingMatrix();
    void testBidirectionalDogfooding();
    void testHostFallbackWithoutSharedImpl();
};

// ── Gating matrix (direct PluginContextImpl construction) ──────────────────

void PluginQueryApiTest::testGatingMatrix()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl sharedControl(&mgr, nullptr);

    bool writeSwitch = true;
    const QStringList granted{QStringLiteral("instance_lifecycle")};
    const QStringList notGranted{QStringLiteral("network")};

    auto makeContext = [&](const QStringList& caps) {
        return std::unique_ptr<core::PluginContextImpl>(
            new core::PluginContextImpl(
                QStringLiteral("org.test.gate"), &rosterApi, &sharedControl,
                [&writeSwitch]() { return writeSwitch; },
                /*pageModel=*/nullptr, /*stream=*/nullptr, base.path(),
                /*downloadService=*/nullptr, caps,
                /*voicePackRefresh=*/{}, /*parent=*/nullptr));
    };

    // 1) granted + switch on → the shared real implementation (identity).
    {
        auto ctx = makeContext(granted);
        pet::IExtApi* api = ctx->queryApi(pet::kInstanceControlApiId, 1);
        QVERIFY(api != nullptr);
        QCOMPARE(api, static_cast<pet::IExtApi*>(&sharedControl));
        auto* control = dynamic_cast<pet::IInstanceControlApi*>(api);
        QVERIFY(control != nullptr);
        QString uuid;
        QCOMPARE(control->create({}, &uuid), pet::PluginError::InvalidArgument);
        pet::InstanceSpec spec;
        spec.label = QStringLiteral("Real");
        QCOMPARE(control->create(spec, &uuid), pet::PluginError::Ok);
        QVERIFY(!uuid.isEmpty());
        QCOMPARE(mgr.rowCount(), 1);
    }

    // 2) granted + switch OFF → capability stub.
    {
        writeSwitch = false;
        auto ctx = makeContext(granted);
        pet::IExtApi* api = ctx->queryApi(pet::kInstanceControlApiId, 1);
        QVERIFY(api != nullptr);
        QVERIFY(api != static_cast<pet::IExtApi*>(&sharedControl));
        auto* control = dynamic_cast<pet::IInstanceControlApi*>(api);
        QVERIFY(control != nullptr);
        QString uuid = QStringLiteral("sentinel");
        QCOMPARE(control->create({}, &uuid), pet::PluginError::Capability);
        QCOMPARE(uuid, QString());
        const int before = mgr.rowCount();
        QCOMPARE(control->remove(QStringLiteral("nope")), pet::PluginError::Capability);
        QCOMPARE(control->start(QStringLiteral("nope")), pet::PluginError::Capability);
        QCOMPARE(control->stop(QStringLiteral("nope")), pet::PluginError::Capability);
        QCOMPARE(control->restart(QStringLiteral("nope")), pet::PluginError::Capability);
        QCOMPARE(control->loadModel(QStringLiteral("nope"), QStringLiteral("M")),
                 pet::PluginError::Capability);
        QCOMPARE(mgr.rowCount(), before); // stub never touched the roster
        writeSwitch = true;
    }

    // 3) NOT granted (switch on) → capability stub.
    {
        auto ctx = makeContext(notGranted);
        pet::IExtApi* api = ctx->queryApi(pet::kInstanceControlApiId, 1);
        QVERIFY(api != nullptr);
        QVERIFY(api != static_cast<pet::IExtApi*>(&sharedControl));
        auto* control = dynamic_cast<pet::IInstanceControlApi*>(api);
        QVERIFY(control != nullptr);
        pet::InstanceSpec spec;
        spec.label = QStringLiteral("X");
        QCOMPARE(control->create(spec, nullptr), pet::PluginError::Capability);
        QCOMPARE(mgr.rowCount(), 1);
    }

    // 4) unknown apiId / null apiId → nullptr.
    {
        auto ctx = makeContext(granted);
        QCOMPARE(ctx->queryApi("pet.does_not_exist", 1), nullptr);
        QCOMPARE(ctx->queryApi(nullptr, 1), nullptr);
    }

    // 5) minVersion above the family version → nullptr ("feature absent").
    {
        auto ctx = makeContext(granted);
        QCOMPARE(ctx->queryApi(pet::kInstanceControlApiId,
                               pet::kInstanceControlApiVersion + 1),
                 nullptr);
        // Exact version is served.
        QVERIFY(ctx->queryApi(pet::kInstanceControlApiId,
                              pet::kInstanceControlApiVersion) != nullptr);
    }

    // 6) degraded wiring (null shared impl) → stub, never a crash.
    {
        core::PluginContextImpl ctx(QStringLiteral("org.test.degraded"),
                                    &rosterApi, /*sharedControl=*/nullptr,
                                    []() { return true; },
                                    nullptr, nullptr, base.path(), nullptr,
                                    granted, {}, nullptr);
        pet::IExtApi* api = ctx.queryApi(pet::kInstanceControlApiId, 1);
        QVERIFY(api != nullptr);
        auto* control = dynamic_cast<pet::IInstanceControlApi*>(api);
        QVERIFY(control != nullptr);
        pet::InstanceSpec spec;
        spec.label = QStringLiteral("X");
        QCOMPARE(control->create(spec, nullptr), pet::PluginError::Capability);
    }
}

// ── Bidirectional dogfooding through a real PluginHost ─────────────────────

void PluginQueryApiTest::testBidirectionalDogfooding()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);
    core::InstanceControlApiImpl sharedControl(&mgr, nullptr);

    PluginRegistry reg;
    reg.addStaticPlugin(manifestWithCapabilities("org.test.lifecycle",
                                                 "\"instance_lifecycle\""),
                        &spawnLifecyclePlugin);
    PluginHost host(reg);
    host.setInstanceManager(&mgr);
    host.setInstanceApi(&rosterApi);
    host.setInstanceControlApi(&sharedControl);
    host.setWriteEnabledProvider([]() { return true; });
    host.initializeAll();
    QVERIFY2(g_lastPlugin != nullptr, "plugin was never initialized");
    QCOMPARE(host.startedIds(), QStringList{QStringLiteral("org.test.lifecycle")});

    // Plugin → host: initialize() got the REAL shared implementation and
    // its create landed in the roster (visible via IInstanceApi AND via
    // the panel's own RosterApiModel — same shared objects production
    // wires).
    QCOMPARE(g_lastPlugin->createResult, pet::PluginError::Ok);
    QVERIFY(!g_lastPlugin->createdUuid.isEmpty());
    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(rosterApi.instances().at(0).label, QStringLiteral("PluginPet"));

    RosterApiModel uiModel(&rosterApi, &sharedControl);
    QCOMPARE(uiModel.rowCount(), 1);
    QCOMPARE(uiModel.data(uiModel.index(0, 0), RosterApiModel::LabelRole).toString(),
             QStringLiteral("PluginPet"));

    // Host → plugin: the UI write bridge creates an instance; the plugin's
    // roster observer (registered in initialize) sees the roster change.
    const int before = g_lastPlugin->observer.rosterChanges;
    QCOMPARE(uiModel.createInstance(QStringLiteral("UiPet"),
                                    QStringLiteral("🐱"), QString(), false),
             static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(mgr.rowCount(), 2);
    QVERIFY(g_lastPlugin->observer.rosterChanges > before);

    // And the roster read side reflects the UI-created instance.
    QCOMPARE(uiModel.rowCount(), 2);
    QCOMPARE(uiModel.data(uiModel.index(1, 0), RosterApiModel::LabelRole).toString(),
             QStringLiteral("UiPet"));

    // UI delete through the same bridge; plugin observer notified again.
    const int beforeDelete = g_lastPlugin->observer.rosterChanges;
    const QString doomed = rosterApi.instances().at(1).uuid;
    QCOMPARE(uiModel.deleteInstance(doomed), static_cast<int>(pet::PluginError::Ok));
    QCOMPARE(mgr.rowCount(), 1);
    QVERIFY(g_lastPlugin->observer.rosterChanges > beforeDelete);

    host.shutdownAll(); // deletes the plugin instance (host ownership)
}

// ── PluginHost fallback: no shared impl injected → per-host impl ────────────

void PluginQueryApiTest::testHostFallbackWithoutSharedImpl()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    core::InstanceApiImpl rosterApi(&mgr, nullptr);

    // No setInstanceControlApi: initializeAll falls back to a per-host
    // InstanceControlApiImpl (the S3 InstanceApiImpl pattern) — a
    // capability-granted plugin still gets the REAL family, not a stub.
    PluginRegistry reg;
    reg.addStaticPlugin(manifestWithCapabilities("org.test.fallback",
                                                 "\"instance_lifecycle\""),
                        &spawnLifecyclePlugin);
    PluginHost host(reg);
    host.setInstanceManager(&mgr);
    host.setInstanceApi(&rosterApi);
    host.initializeAll();
    QCOMPARE(g_lastPlugin->createResult, pet::PluginError::Ok);
    QCOMPARE(mgr.rowCount(), 1);
    host.shutdownAll();
}

QTEST_MAIN(PluginQueryApiTest)
#include "PluginQueryApiTest.moc"
