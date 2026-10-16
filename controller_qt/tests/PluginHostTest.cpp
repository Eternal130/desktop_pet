// PluginHostTest (P4, §A.1 + §B.3): initialize/shutdown lifecycle —
// exception isolation (a throwing or error-returning plugin NEVER poisons
// the next), manifest order, reverse shutdown, budget exhaustion, and the
// disabled-skip semantics (config bit → no create() at all).
#include <QThread>
#include <QTest>

#include "core/PluginHost.hpp"
#include "core/PluginPageModel.hpp"

using core::PluginEntry;
using core::PluginHost;
using core::PluginRegistry;
using core::PluginStatus;

namespace {

// ── Fakes: record calls in file-static vectors (factory functions are
// plain function pointers — no capturing state possible).
struct FakePlugin;
QVector<FakePlugin*> g_created; // host-owned; tests delete leftovers

struct FakePlugin final : public pet::IPanelPlugin
{
    enum Behavior { Ok, ReturnError, ThrowOnInit, SlowShutdown, ThrowOnShutdown };
    Behavior behavior = Ok;
    bool initialized = false;
    bool shutDown = false;

    // The HOST deletes instances during shutdownAll (production ownership);
    // self-removal keeps g_created consistent so the test cleanup only
    // deletes what the host did NOT take.
    ~FakePlugin() { g_created.removeAll(this); }

    pet::PluginError initialize(pet::IPluginContext&) override
    {
        initialized = true;
        if (behavior == ReturnError)
            return pet::PluginError::Generic;
        if (behavior == ThrowOnInit)
            throw std::runtime_error("kaboom from initialize");
        return pet::PluginError::Ok;
    }

    void shutdown() override
    {
        shutDown = true;
        if (behavior == SlowShutdown)
            QThread::msleep(30); // exceed the test budget
        if (behavior == ThrowOnShutdown)
            throw std::runtime_error("kaboom from shutdown");
    }
};

pet::IPanelPlugin* spawn(FakePlugin::Behavior b)
{
    auto* p = new FakePlugin;
    p->behavior = b;
    g_created.append(p);
    return p;
}

// Factories INSIDE the anon namespace — file-local function pointers for
// PluginRegistry::addStaticPlugin (declaring them outside would create a
// second, ambiguous overload set at global scope).
pet::IPanelPlugin* createOk()            { return spawn(FakePlugin::Ok); }
pet::IPanelPlugin* createError()         { return spawn(FakePlugin::ReturnError); }
pet::IPanelPlugin* createThrow()         { return spawn(FakePlugin::ThrowOnInit); }
pet::IPanelPlugin* createSlowShutdown()  { return spawn(FakePlugin::SlowShutdown); }
pet::IPanelPlugin* createThrowShutdown() { return spawn(FakePlugin::ThrowOnShutdown); }
// P0-1 (review round): a static CreateFn yielding a null instance.
pet::IPanelPlugin* createNull()          { return nullptr; }

// ── Stage-2 dynamic fakes (2026-10-16): FakeFactory accounts create/destroy
// so the tests can prove the host routes dynamic entries THROUGH the module
// factory — and NEVER deletes a dynamic instance directly (P6a criterion 5).
int g_dynCreated = 0;
int g_dynDestroyed = 0;  // destroy() calls (the legal path)
int g_dynDeleted = 0;    // instance deletions (either path — dtor hook)

// FakeDynPlugin fail modes (review round: initialize-failure teardown).
enum class DynFail { None, ReturnError, ThrowOnInit };

struct FakeDynPlugin final : public pet::IPanelPlugin
{
    DynFail fail = DynFail::None;

    pet::PluginError initialize(pet::IPluginContext&) override
    {
        if (fail == DynFail::ReturnError)
            return pet::PluginError::Generic;
        if (fail == DynFail::ThrowOnInit)
            throw std::runtime_error("kaboom from dyn initialize");
        return pet::PluginError::Ok;
    }
    void shutdown() override {}
    ~FakeDynPlugin() { ++g_dynDeleted; }
};

class FakeFactory : public QObject, public pet::IPluginFactory
{
    Q_OBJECT
    Q_INTERFACES(pet::IPluginFactory)

public:
    bool throwOnCreate = false;
    bool returnNull = false;       // P0-1: create() yields a null instance
    DynFail instanceFail = DynFail::None; // P1-3: created plugin's behavior

    pet::IPanelPlugin* create() override
    {
        if (throwOnCreate)
            throw std::runtime_error("kaboom from factory create");
        if (returnNull)
            return nullptr; // no instance — not counted as created
        ++g_dynCreated;
        auto* p = new FakeDynPlugin;
        p->fail = instanceFail;
        return p;
    }

    void destroy(pet::IPanelPlugin* plugin) override
    {
        // Module-side destruction: if the host had already deleted the
        // instance directly, this would double-delete (crash) — and the
        // g_dynDestroyed counter would stay 0 if destroy was never called.
        ++g_dynDestroyed;
        delete plugin;
    }
};

} // namespace

class PluginHostTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { g_created.clear(); }
    void cleanup() { qDeleteAll(g_created); g_created.clear(); }

    void testInitializeOrderAndIsolation();
    void testDisabledPluginSkippedBeforeCreate();
    void testShutdownReverseOrderAndExceptionIsolation();
    void testShutdownBudgetExhaustion();
    // Stage 2 (2026-10-16): dynamic entries live and die through the
    // module-side IPluginFactory (create/destroy), never host-side.
    void testDynamicPluginLifecycleThroughFactory();
    void testDynamicDisabledSkippedBeforeFactoryCreate();
    void testDynamicFactoryThrowIsIsolated();
    // Review round (2026-10-16): P0-1 null-instance isolation + P1-3
    // destroy-on-initialize-failure semantics.
    void testNullInstanceFromFactoryIsIsolated();
    void testInitializeFailureDestroysInstance();
};

namespace {
QString manifestFor(const char* id, int order)
{
    return QStringLiteral(
        R"json({"id":"%1","version":"1.0","api_version":"1.0","order":%2})json")
        .arg(QLatin1String(id)).arg(order);
}
} // namespace

void PluginHostTest::testInitializeOrderAndIsolation()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.b", 200), &createError);
    reg.addStaticPlugin(manifestFor("org.test.a", 100), &createOk);
    reg.addStaticPlugin(manifestFor("org.test.c", 300), &createThrow);
    PluginHost host(reg);
    host.initializeAll();

    QCOMPARE(host.startedIds(), QStringList{QStringLiteral("org.test.a")});
    QCOMPARE(reg.entry("org.test.a")->status, PluginStatus::Started);
    QCOMPARE(reg.entry("org.test.b")->status, PluginStatus::Failed);
    QVERIFY(reg.entry("org.test.b")->errorMessage.contains(QStringLiteral("error")));
    QCOMPARE(reg.entry("org.test.c")->status, PluginStatus::Failed);
    QVERIFY(reg.entry("org.test.c")->errorMessage.contains(QStringLiteral("threw")));
    QCOMPARE(host.failedCount(), 2);
    host.shutdownAll();
}

void PluginHostTest::testDisabledPluginSkippedBeforeCreate()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.off", 100), &createOk);
    reg.addStaticPlugin(manifestFor("org.test.on", 200), &createOk);
    PluginHost host(reg);
    host.setEnabledProvider([](const QString& id) {
        return id != QStringLiteral("org.test.off");
    });
    host.initializeAll();

    // disabled: no create() AT ALL (§B.6 — no code from the plugin runs)
    QCOMPARE(g_created.size(), 1);
    QCOMPARE(reg.entry("org.test.off")->status, PluginStatus::Registered);
    QCOMPARE(reg.entry("org.test.off")->instance, nullptr);
    QCOMPARE(reg.entry("org.test.on")->status, PluginStatus::Started);
    host.shutdownAll();
}

void PluginHostTest::testShutdownReverseOrderAndExceptionIsolation()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifestFor("org.test.a", 100), &createOk);
    reg.addStaticPlugin(manifestFor("org.test.b", 200), &createThrowShutdown);
    reg.addStaticPlugin(manifestFor("org.test.c", 300), &createOk);
    PluginHost host(reg);
    host.initializeAll();
    QCOMPARE(host.startedIds().size(), 3);

    host.shutdownAll();
    // reverse order: c, b (throws — isolated), a all reached Stopped
    QCOMPARE(reg.entry("org.test.c")->status, PluginStatus::Stopped);
    QCOMPARE(reg.entry("org.test.b")->status, PluginStatus::Stopped);
    QCOMPARE(reg.entry("org.test.a")->status, PluginStatus::Stopped);
    for (FakePlugin* p : g_created)
        QVERIFY(p->shutDown);
    QVERIFY(host.startedIds().isEmpty());
}

void PluginHostTest::testShutdownBudgetExhaustion()
{
    PluginRegistry reg;
    // fast (order 100) starts first → shuts down LAST; slow (order 200)
    // starts last → shuts down FIRST and exhausts the budget → fast is
    // skipped with a WARN (never fatal, never blocks exit).
    reg.addStaticPlugin(manifestFor("org.test.fast", 100), &createOk);
    reg.addStaticPlugin(manifestFor("org.test.slow", 200), &createSlowShutdown);
    PluginHost host(reg);
    host.initializeAll();
    host.setShutdownBudgetMs(20);
    host.shutdownAll();
    QCOMPARE(reg.entry("org.test.slow")->status, PluginStatus::Stopped);
    QVERIFY(g_created.at(1)->shutDown); // slow ran (and burned the budget)
    // fast plugin's shutdown was skipped: still Started, instance intact
    QCOMPARE(reg.entry("org.test.fast")->status, PluginStatus::Started);
    QVERIFY(!g_created.at(0)->shutDown);
}

void PluginHostTest::testDynamicPluginLifecycleThroughFactory()
{
    g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
    FakeFactory factory;
    PluginRegistry reg;
    // The real generated host baseline must pass its own gate.
    reg.addDynamicPlugin(manifestFor("org.test.dyn", 100), &factory,
                         QStringLiteral("/fake/libdyn.so"), core::hostAbiBlock());
    QCOMPARE(reg.entry("org.test.dyn")->status, PluginStatus::Registered);
    QCOMPARE(reg.entry("org.test.dyn")->source, QStringLiteral("dynamic"));

    PluginHost host(reg);
    host.initializeAll();
    // constructed THROUGH the factory (module-side), not host-side
    QCOMPARE(g_dynCreated, 1);
    QCOMPARE(reg.entry("org.test.dyn")->status, PluginStatus::Started);
    QVERIFY(reg.entry("org.test.dyn")->instance != nullptr);

    host.shutdownAll();
    // destroyed THROUGH factory->destroy exactly once; the host never
    // deleted the instance directly (destroy would then never fire —
    // or double-delete and crash the test).
    QCOMPARE(g_dynCreated, 1);
    QCOMPARE(g_dynDestroyed, 1);
    QCOMPARE(g_dynDeleted, 1);
    QCOMPARE(reg.entry("org.test.dyn")->status, PluginStatus::Stopped);
    QVERIFY(reg.entry("org.test.dyn")->instance == nullptr);
}

void PluginHostTest::testDynamicDisabledSkippedBeforeFactoryCreate()
{
    g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
    FakeFactory factory;
    PluginRegistry reg;
    reg.addDynamicPlugin(manifestFor("org.test.dynoff", 100), &factory,
                         QStringLiteral("/fake/libdynoff.so"),
                         core::hostAbiBlock());
    PluginHost host(reg);
    host.setEnabledProvider([](const QString& id) {
        return id != QStringLiteral("org.test.dynoff");
    });
    host.initializeAll();

    // disabled = config bit → skip BEFORE create: the factory's create()
    // runs ZERO times (§B.6 — no code from the plugin executes at all).
    QCOMPARE(g_dynCreated, 0);
    QCOMPARE(reg.entry("org.test.dynoff")->status, PluginStatus::Registered);
    QCOMPARE(reg.entry("org.test.dynoff")->instance, nullptr);
    host.shutdownAll();
    QCOMPARE(g_dynDestroyed, 0);
}

void PluginHostTest::testDynamicFactoryThrowIsIsolated()
{
    g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
    FakeFactory throwing;
    throwing.throwOnCreate = true;
    FakeFactory healthy;
    PluginRegistry reg;
    reg.addDynamicPlugin(manifestFor("org.test.dynbad", 100), &throwing,
                         QStringLiteral("/fake/libdynbad.so"),
                         core::hostAbiBlock());
    reg.addStaticPlugin(manifestFor("org.test.dynnext", 200), &createOk);
    PluginHost host(reg);
    host.initializeAll();

    // factory create() threw → entry Failed, isolated; the NEXT plugin
    // still initializes (§A.1 — a bad plugin never poisons the pump).
    QCOMPARE(g_dynCreated, 0);
    QCOMPARE(reg.entry("org.test.dynbad")->status, PluginStatus::Failed);
    QVERIFY(reg.entry("org.test.dynbad")->errorMessage.contains(
        QStringLiteral("threw")));
    QCOMPARE(reg.entry("org.test.dynnext")->status, PluginStatus::Started);
    host.shutdownAll();
}

void PluginHostTest::testNullInstanceFromFactoryIsIsolated()
{
    g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
    FakeFactory nullFactory;
    nullFactory.returnNull = true;
    PluginRegistry reg;
    // Dynamic factory → null; static CreateFn → null; a healthy static
    // plugin proves the scan continues past both.
    reg.addDynamicPlugin(manifestFor("org.test.dynnull", 100), &nullFactory,
                         QStringLiteral("/fake/libdynnull.so"),
                         core::hostAbiBlock());
    reg.addStaticPlugin(manifestFor("org.test.statnull", 200), &createNull);
    reg.addStaticPlugin(manifestFor("org.test.afternull", 300), &createOk);
    PluginHost host(reg);
    host.initializeAll();

    // P0-1: both null-producing factories fail CLEANLY (no context, no
    // initialize on a null instance) and never poison the next plugin.
    QCOMPARE(reg.entry("org.test.dynnull")->status, PluginStatus::Failed);
    QVERIFY(reg.entry("org.test.dynnull")->errorMessage.contains(
        QStringLiteral("null instance")));
    QCOMPARE(reg.entry("org.test.dynnull")->instance, nullptr);
    QCOMPARE(reg.entry("org.test.statnull")->status, PluginStatus::Failed);
    QVERIFY(reg.entry("org.test.statnull")->errorMessage.contains(
        QStringLiteral("null instance")));
    QCOMPARE(reg.entry("org.test.afternull")->status, PluginStatus::Started);
    QCOMPARE(host.startedIds(), QStringList{QStringLiteral("org.test.afternull")});
    // No instance was ever produced → no destroy ever due.
    QCOMPARE(g_dynCreated, 0);
    QCOMPARE(g_dynDestroyed, 0);
    host.shutdownAll();
}

void PluginHostTest::testInitializeFailureDestroysInstance()
{
    // P1-3 (two variants, one slot): initialize returning an error AND
    // initialize throwing both destroy the created instance NOW —
    // dynamic entries through factory->destroy exactly once, instance
    // nulled, entry Failed — instead of leaking until shutdownAll (which
    // only visits STARTED plugins).
    {
        g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
        FakeFactory factory;
        factory.instanceFail = DynFail::ReturnError;
        PluginRegistry reg;
        reg.addDynamicPlugin(manifestFor("org.test.dynerr", 100), &factory,
                             QStringLiteral("/fake/libdynerr.so"),
                             core::hostAbiBlock());
        PluginHost host(reg);
        host.initializeAll();

        QCOMPARE(g_dynCreated, 1);
        QCOMPARE(g_dynDestroyed, 1);  // destroy on the failure path
        QCOMPARE(g_dynDeleted, 1);    // ...and the instance died exactly once
        QCOMPARE(reg.entry("org.test.dynerr")->instance, nullptr);
        QCOMPARE(reg.entry("org.test.dynerr")->status, PluginStatus::Failed);
        QVERIFY(reg.entry("org.test.dynerr")->errorMessage.contains(
            QStringLiteral("error")));
        host.shutdownAll(); // nothing started → no double destroy
        QCOMPARE(g_dynDestroyed, 1);
    }
    {
        g_dynCreated = g_dynDestroyed = g_dynDeleted = 0;
        FakeFactory factory;
        factory.instanceFail = DynFail::ThrowOnInit;
        PluginRegistry reg;
        reg.addDynamicPlugin(manifestFor("org.test.dynthrow", 100), &factory,
                             QStringLiteral("/fake/libdynthrow.so"),
                             core::hostAbiBlock());
        PluginHost host(reg);
        host.initializeAll();

        QCOMPARE(g_dynCreated, 1);
        QCOMPARE(g_dynDestroyed, 1);
        QCOMPARE(g_dynDeleted, 1);
        QCOMPARE(reg.entry("org.test.dynthrow")->instance, nullptr);
        QCOMPARE(reg.entry("org.test.dynthrow")->status, PluginStatus::Failed);
        QVERIFY(reg.entry("org.test.dynthrow")->errorMessage.contains(
            QStringLiteral("threw")));
        host.shutdownAll();
        QCOMPARE(g_dynDestroyed, 1);
    }
}

QTEST_MAIN(PluginHostTest)
#include "PluginHostTest.moc"
