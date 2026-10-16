// DynamicPluginSourceTest (stage 2 host channel, 2026-10-16): drop-in
// discovery over the ILibraryLoader/ILibraryHandle seams — fake handles
// feed metaData()/instance() and count instance() calls externally (the
// counters survive handle destruction, so "zero library code on
// rejection" is assertable even when the source destroys the rejected
// handle). Locks the §A.2 ordering invariant (metaData before instance),
// the gate matrix (iid / api_version / abi / duplicate id / root cast),
// the silent-missing-directory contract, and the exe-dir-over-data-dir
// same-name priority.
// QTEST_APPLESS_MAIN — pure logic + QObject fakes over injected search
// dirs (production defaults need QCoreApplication/ConfigDir, never touched
// here), no event loop needed.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <stdexcept>

#include "api/IPluginFactory.hpp"
#include "core/DynamicPluginSource.hpp"
#include "core/PluginRegistry.hpp"

using core::DynamicPluginSource;
using core::ILibraryHandle;
using core::ILibraryLoader;
using core::PluginEntry;
using core::PluginRegistry;
using core::PluginStatus;

namespace {

#ifdef Q_OS_WIN
const char* kLibExt = ".dll";
#else
const char* kLibExt = ".so";
#endif

// ── Fakes ───────────────────────────────────────────────────────────────────

// Fake factory root: a QObject implementing pet::IPluginFactory (the
// qobject_cast target of the production path).
class FakeFactory : public QObject, public pet::IPluginFactory
{
    Q_OBJECT
    Q_INTERFACES(pet::IPluginFactory)

public:
    pet::IPanelPlugin* create() override { return nullptr; }
    void destroy(pet::IPanelPlugin*) override {}
};

struct FakeHandle;

// Test-owned ledger of live handles (the source takes ownership on
// open(); rejected handles are destroyed by the source, accepted ones
// live in the source until it is destroyed).
QList<FakeHandle*>* g_ledger = nullptr;

struct FakeHandle final : public ILibraryHandle
{
    QString path;
    QJsonObject meta;
    QObject* root = nullptr;
    int* instanceCalls = nullptr; // external counter (outlives the handle)
    bool preventUnload = false;
    // Review round: P0-2 (instance() throws) + P1-2 (load failure with a
    // loader error string).
    bool throwOnInstance = false;
    QString error = QStringLiteral("fake error");

    ~FakeHandle() override
    {
        if (g_ledger)
            g_ledger->removeAll(this);
    }

    QJsonObject pluginMetaData() const override { return meta; }
    QObject* instance() override
    {
        if (instanceCalls)
            ++*instanceCalls;
        if (throwOnInstance)
            throw std::runtime_error("kaboom from root construction");
        return root;
    }
    QString errorString() const override { return error; }
    void setPreventUnload() override { preventUnload = true; }
};

class FakeLoader final : public ILibraryLoader
{
public:
    QList<FakeHandle*> stock; // path-matched; ownership passes on open()
    QStringList opened;

    std::unique_ptr<ILibraryHandle> open(const QString& filePath) override
    {
        opened.append(filePath);
        for (int i = 0; i < stock.size(); ++i) {
            if (stock.at(i)->path == filePath) {
                FakeHandle* h = stock.takeAt(i);
                return std::unique_ptr<ILibraryHandle>(h);
            }
        }
        return nullptr;
    }
};

// ── Helpers ─────────────────────────────────────────────────────────────────

QJsonObject hostAbiObject()
{
    return QJsonDocument::fromJson(core::hostAbiBlock().toUtf8()).object();
}

// Module metaData EXACTLY as Qt 6.10 QPluginLoader reports it: "IID" at
// the top level (capitalized — verified empirically; the lowercase
// spelling silently returns nothing), the plugin.json content nested
// under "MetaData" (abi included). Fakes must mirror the REAL casing so
// they exercise the production contract, not a self-consistent illusion
// (the lowercase variant once made all fakes pass while real loads
// failed — caught by the drop-in smoke, 2026-10-16).
QJsonObject makeMetaData(const QString& id, const QJsonObject& abi,
                         const QString& apiVersion, const char* iid)
{
    QJsonObject manifest{
        {"id", id},
        {"version", QStringLiteral("0.1.0")},
        {"api_version", apiVersion},
    };
    if (!abi.isEmpty())
        manifest.insert(QStringLiteral("abi"), abi);
    return QJsonObject{
        {"IID", QString::fromLatin1(iid)},
        {"MetaData", manifest},
    };
}

QJsonObject makeMetaData(const QString& id, const QJsonObject& abi)
{
    return makeMetaData(id, abi, QStringLiteral("1.0"),
                        "org.desktop-pet.PanelPluginFactory/1.0");
}

QString manifestJsonFor(const QString& id, int order = 100)
{
    return QStringLiteral(
        R"json({"id":"%1","version":"0.1.0","api_version":"1.0","order":%2})json")
        .arg(id).arg(order);
}

} // namespace

class DynamicPluginSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void init() { g_ledger = &m_handles; m_handles.clear(); }
    void cleanup()
    {
        // Delete over a COPY: each dtor self-removes from m_handles
        // (the ledger pattern from PluginHostTest's FakePlugin).
        const QList<FakeHandle*> leftovers = m_handles;
        for (FakeHandle* h : leftovers)
            delete h;
        m_handles.clear();
        g_ledger = nullptr;
    }

    void testValidDynamicPluginFullChain();
    void testUnsupportedIidFailsWithoutInstance();
    void testAbiMismatchFails();
    void testTooNewApiVersionFails();
    void testDuplicateStaticIdRejected();
    void testUncastableRootFails();
    void testMissingDirectoriesAreSilent();
    void testExeDirWinsSameNameOverDataDir();
    // Review round (2026-10-16): P0-2 instance() throw isolation, P1-1
    // synthetic-id collision guard, P1-2 load-failure errorString split.
    void testInstanceThrowIsolated();
    void testSyntheticIdNeverOverwritesLegitimateEntry();
    void testLoadFailureSurfacesErrorString();

private:
    // Create a library FILE on disk (the scan only sees real files), stock
    // a matching fake handle in the loader, and hand the loader's ownership
    // to a fresh source wired to `dirs`. Returns the file path (empty on
    // failure); the raw loader/view stays valid for post-discover asserts.
    QString stockFile(FakeLoader* loader, const QString& dir,
                      const QString& baseName, const QJsonObject& meta,
                      QObject* root, int* instanceCalls)
    {
        if (!QDir(dir).mkpath(QStringLiteral(".")))
            return QString();
        const QString path =
            QDir(dir).absoluteFilePath(baseName + QLatin1String(kLibExt));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write("dummy");
        f.close();

        auto* h = new FakeHandle;
        h->path = path;
        h->meta = meta;
        h->root = root;
        h->instanceCalls = instanceCalls;
        m_handles.append(h);
        loader->stock.append(h);
        return path;
    }

    QList<FakeHandle*> m_handles;
};

void DynamicPluginSourceTest::testValidDynamicPluginFullChain()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    FakeFactory factory;
    int instanceCalls = 0;
    const QString path = stockFile(
        loader.get(), tmp.path(), QStringLiteral("good"),
        makeMetaData(QStringLiteral("org.test.dyn"), hostAbiObject()),
        &factory, &instanceCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    QCOMPARE(reg.size(), 1);
    const PluginEntry* e = reg.entry(QStringLiteral("org.test.dyn"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->source, QStringLiteral("dynamic"));
    QCOMPARE(e->status, PluginStatus::Registered);
    QCOMPARE(e->libraryPath, path);
    // factory backfilled after instance() + qobject_cast (register-then-
    // backfill) — exactly one instance() call, prevent-unload asserted.
    QCOMPARE(e->factory, static_cast<pet::IPluginFactory*>(&factory));
    QCOMPARE(instanceCalls, 1);
    QCOMPARE(m_handles.size(), 1); // the accepted handle stays alive
    QVERIFY(m_handles.first()->preventUnload);
}

void DynamicPluginSourceTest::testUnsupportedIidFailsWithoutInstance()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    stockFile(loader.get(), tmp.path(), QStringLiteral("badiid"),
              makeMetaData(QStringLiteral("org.test.x"), hostAbiObject(),
                           QStringLiteral("1.0"), "com.other.Plugin/2.0"),
              nullptr, &instanceCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    // Visible Failed entry under the synthetic dropin.<basename> id —
    // never silently dropped, and ZERO instance() calls.
    QCOMPARE(reg.size(), 1);
    const PluginEntry* e = reg.entry(QStringLiteral("dropin.badiid"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QVERIFY(e->errorMessage.contains(QStringLiteral("unsupported iid")));
    QCOMPARE(e->source, QStringLiteral("dynamic"));
    QCOMPARE(instanceCalls, 0);
}

void DynamicPluginSourceTest::testAbiMismatchFails()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    // Same block as the host baseline with compiler_version mutated.
    QJsonObject abi = hostAbiObject();
    const QString cv = abi.value(QStringLiteral("compiler_version")).toString();
    abi["compiler_version"] =
        cv.isEmpty() ? QStringLiteral("0.0.0-mutated")
                     : (cv + QStringLiteral("x"));
    stockFile(loader.get(), tmp.path(), QStringLiteral("wrongabi"),
              makeMetaData(QStringLiteral("org.test.wrongabi"), abi),
              nullptr, &instanceCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.wrongabi"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QVERIFY(e->errorMessage.contains(QStringLiteral("abi")));
    QCOMPARE(instanceCalls, 0);
}

void DynamicPluginSourceTest::testTooNewApiVersionFails()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    stockFile(loader.get(), tmp.path(), QStringLiteral("toonew"),
              makeMetaData(QStringLiteral("org.test.toonew"), hostAbiObject(),
                           QStringLiteral("99.0"),
                           "org.desktop-pet.PanelPluginFactory/1.0"),
              nullptr, &instanceCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.toonew"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QVERIFY(e->errorMessage.contains(QStringLiteral("exceeds")));
    QCOMPARE(instanceCalls, 0);
}

void DynamicPluginSourceTest::testDuplicateStaticIdRejected()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    stockFile(loader.get(), tmp.path(), QStringLiteral("dup"),
              makeMetaData(QStringLiteral("org.test.dup"), hostAbiObject()),
              nullptr, &instanceCalls);

    PluginRegistry reg;
    // Static registration FIRST (production order: main registers the
    // CMake table before discover()).
    reg.addStaticPlugin(manifestJsonFor(QStringLiteral("org.test.dup")),
                        nullptr);

    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    // Two entries: the ORIGINAL static one untouched, plus the visible
    // Failed duplicate (never silently dropped, never overwriting).
    QCOMPARE(reg.size(), 2);
    const PluginEntry* original = reg.entry(QStringLiteral("org.test.dup"));
    QVERIFY(original != nullptr);
    QCOMPARE(original->source, QStringLiteral("static"));
    QCOMPARE(original->status, PluginStatus::Registered);
    int failedDuplicates = 0;
    for (const PluginEntry& en : reg.orderedEntries()) {
        if (en.source == QStringLiteral("dynamic")) {
            QCOMPARE(en.status, PluginStatus::Failed);
            QVERIFY(en.errorMessage.contains(QStringLiteral("duplicate")));
            ++failedDuplicates;
        }
    }
    QCOMPARE(failedDuplicates, 1);
    QCOMPARE(instanceCalls, 0);
}

void DynamicPluginSourceTest::testUncastableRootFails()
{
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    QObject plainRoot; // a QObject that does NOT implement IPluginFactory
    int instanceCalls = 0;
    stockFile(loader.get(), tmp.path(), QStringLiteral("noroot"),
              makeMetaData(QStringLiteral("org.test.noroot"), hostAbiObject()),
              &plainRoot, &instanceCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.noroot"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QVERIFY(e->errorMessage.contains(QStringLiteral("IPluginFactory")));
    QCOMPARE(e->factory, nullptr);
    // instance() WAS called here (every metadata gate passed) — the root
    // simply does not implement the factory interface.
    QCOMPARE(instanceCalls, 1);
}

void DynamicPluginSourceTest::testMissingDirectoriesAreSilent()
{
    auto loader = std::make_unique<FakeLoader>();
    FakeLoader* view = loader.get();

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({QStringLiteral("/nonexistent/desktop-pet-test/a"),
                          QStringLiteral("/nonexistent/desktop-pet-test/b")});
    source.discover(reg);

    QCOMPARE(reg.size(), 0); // silent: no entries, no creation, no crash
    QVERIFY(view->opened.isEmpty());
}

void DynamicPluginSourceTest::testExeDirWinsSameNameOverDataDir()
{
    QTemporaryDir exeTmp;
    QTemporaryDir dataTmp;
    auto loader = std::make_unique<FakeLoader>();
    FakeLoader* view = loader.get();
    FakeFactory exeFactory;
    int exeCalls = 0;
    int dataCalls = 0;
    const QString exePath = stockFile(
        loader.get(), exeTmp.path(), QStringLiteral("same"),
        makeMetaData(QStringLiteral("org.test.fromexe"), hostAbiObject()),
        &exeFactory, &exeCalls);
    stockFile(loader.get(), dataTmp.path(), QStringLiteral("same"),
              makeMetaData(QStringLiteral("org.test.fromdata"), hostAbiObject()),
              nullptr, &dataCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    // Priority order: exe dir first, data dir second (production order).
    source.setSearchDirs({exeTmp.path(), dataTmp.path()});
    source.discover(reg);

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.fromexe"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->libraryPath, exePath);
    QCOMPARE(e->status, PluginStatus::Registered);
    // The lower-priority same-NAME copy was never even opened.
    QVERIFY(reg.entry(QStringLiteral("org.test.fromdata")) == nullptr);
    QCOMPARE(view->opened, QStringList{exePath});
    QCOMPARE(exeCalls, 1);
    QCOMPARE(dataCalls, 0);
}

void DynamicPluginSourceTest::testInstanceThrowIsolated()
{
    // P0-2: instance() runs module code (root construction + static
    // initializers) — an escaping exception must mark the entry Failed,
    // never abort the boot scan.
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    stockFile(
        loader.get(), tmp.path(), QStringLiteral("thrower"),
        makeMetaData(QStringLiteral("org.test.thrower"), hostAbiObject()),
        nullptr, &instanceCalls);
    // The loader stock list holds raw pointers — reach in to arm the throw
    // (ownership has not been transferred yet, the source has not run).
    QVERIFY(!loader->stock.isEmpty());
    loader->stock.first()->throwOnInstance = true;

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg); // must SURVIVE the throw

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.thrower"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QVERIFY(e->errorMessage.contains(QStringLiteral("threw")));
    QCOMPARE(e->factory, nullptr);
    QCOMPARE(instanceCalls, 1);
}

void DynamicPluginSourceTest::testSyntheticIdNeverOverwritesLegitimateEntry()
{
    // P1-1: a LEGITIMATE dynamic plugin whose id happens to equal the
    // synthetic id an unrelated rejected file would use — the guard must
    // keep the legitimate entry Registered (factory intact) instead of
    // flipping it to Failed.
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    FakeFactory legitFactory;
    int legitCalls = 0;
    int badCalls = 0;
    // Legit file "alegit.so" (sorts BEFORE "foo.so" — QDir::Name order
    // decides processing order) carries manifest id "dropin.foo" (valid
    // iid + host abi → Registered, factory backfilled).
    stockFile(loader.get(), tmp.path(), QStringLiteral("alegit"),
              makeMetaData(QStringLiteral("dropin.foo"), hostAbiObject()),
              &legitFactory, &legitCalls);
    // Rejected file "foo.so" (bad iid) synthesizes id "dropin.foo" — the
    // COLLISION the guard exists for.
    stockFile(loader.get(), tmp.path(), QStringLiteral("foo"),
              makeMetaData(QStringLiteral("org.test.x"), hostAbiObject(),
                           QStringLiteral("1.0"), "com.other.Plugin/2.0"),
              nullptr, &badCalls);

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    // The legitimate entry survives untouched — still Registered with its
    // backfilled factory; the rejected file only produced a (duplicate-id)
    // Failed row of its own.
    const PluginEntry* legit = reg.entry(QStringLiteral("dropin.foo"));
    QVERIFY(legit != nullptr);
    QCOMPARE(legit->status, PluginStatus::Registered);
    QCOMPARE(legit->factory, static_cast<pet::IPluginFactory*>(&legitFactory));
    QCOMPARE(reg.size(), 2);
    int failedRows = 0;
    for (const PluginEntry& en : reg.orderedEntries()) {
        if (en.status == PluginStatus::Failed) {
            QVERIFY(en.errorMessage.contains(QStringLiteral("duplicate")));
            ++failedRows;
        }
    }
    QCOMPARE(failedRows, 1);
    QCOMPARE(legitCalls, 1);
    QCOMPARE(badCalls, 0);
}

void DynamicPluginSourceTest::testLoadFailureSurfacesErrorString()
{
    // P1-2: instance() returning null = LOAD failure (missing dependency /
    // wrong architecture) — the loader's errorString is the reason, NOT a
    // cast failure message.
    QTemporaryDir tmp;
    auto loader = std::make_unique<FakeLoader>();
    int instanceCalls = 0;
    stockFile(
        loader.get(), tmp.path(), QStringLiteral("badlib"),
        makeMetaData(QStringLiteral("org.test.badlib"), hostAbiObject()),
        nullptr, &instanceCalls);
    // root stays nullptr (load fails); give the handle a realistic error.
    QVERIFY(!loader->stock.isEmpty());
    FakeHandle* h = loader->stock.first();
    h->root = nullptr; // explicit: instance() will return null
    h->error = QStringLiteral("missing dependency: libfoo.so.2");

    PluginRegistry reg;
    DynamicPluginSource source;
    source.setLoader(std::move(loader));
    source.setSearchDirs({tmp.path()});
    source.discover(reg);

    const PluginEntry* e = reg.entry(QStringLiteral("org.test.badlib"));
    QVERIFY(e != nullptr);
    QCOMPARE(e->status, PluginStatus::Failed);
    QCOMPARE(e->errorMessage, QStringLiteral("missing dependency: libfoo.so.2"));
    QVERIFY(!e->errorMessage.contains(QStringLiteral("IPluginFactory")));
    QCOMPARE(e->factory, nullptr);
}

QTEST_APPLESS_MAIN(DynamicPluginSourceTest)
#include "DynamicPluginSourceTest.moc"
