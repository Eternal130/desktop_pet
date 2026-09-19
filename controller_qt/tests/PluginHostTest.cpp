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

QTEST_MAIN(PluginHostTest)
#include "PluginHostTest.moc"
