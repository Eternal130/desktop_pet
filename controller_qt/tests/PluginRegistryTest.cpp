// PluginRegistryTest (P4): manifest parsing/validation + api_version + abi
// gating + the state machine — the runtime schema authority (§B.6). The
// configure-time CMake check is field-existence only; every rule below is
// THIS class's contract.
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include "core/PluginRegistry.hpp"

using core::PluginRegistry;
using core::PluginStatus;

namespace {

// A complete, valid manifest (kApiMajor.kApiMinor by default).
QString manifest(int apiMajor = pet::kApiMajor, int apiMinor = pet::kApiMinor)
{
    return QStringLiteral(R"json({
        "id": "org.desktop-pet.sample",
        "version": "0.1.0",
        "api_version": "%1.%2",
        "capabilities": ["voicepacks_install"],
        "entry.qml": "SamplePage.qml",
        "title": "示例插件",
        "icon": "",
        "order": 100,
        "vendor": "desktop-pet"
    })json").arg(apiMajor).arg(apiMinor);
}

} // namespace

class PluginRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void testParseValidManifest();
    void testParseRejectsCorruptJson();
    void testParseRejectsMissingFields();
    void testParseRejectsBadIdShape();
    void testParseRejectsNonNumericApiVersion();
    void testApiVersionGating();
    void testAbiGating();
    void testAbiQtVersionRule();
    void testStateMachineLegalChain();
    void testStateMachineRejectsIllegalTransitions();
    void testAddStaticPluginFailedEntryStaysVisible();
    void testAddStaticPluginRejectsTooHighApiVersion();
    void testOrderedEntriesSortByManifestOrder();
};

void PluginRegistryTest::testParseValidManifest()
{
    QString err;
    const auto m = PluginRegistry::parseManifest(manifest(), &err);
    QVERIFY2(m.has_value(), qPrintable("parse failed: " + err));
    QCOMPARE(m->id, QStringLiteral("org.desktop-pet.sample"));
    QCOMPARE(m->version, QStringLiteral("0.1.0"));
    QCOMPARE(m->apiMajor, pet::kApiMajor);
    QCOMPARE(m->apiMinor, pet::kApiMinor);
    QCOMPARE(m->capabilities, QStringList{QStringLiteral("voicepacks_install")});
    QCOMPARE(m->title, QStringLiteral("示例插件"));
    QCOMPARE(m->entryQml, QStringLiteral("SamplePage.qml"));
    QCOMPARE(m->order, 100);
    QVERIFY(err.isEmpty());
}

void PluginRegistryTest::testParseRejectsCorruptJson()
{
    QString err;
    QVERIFY(!PluginRegistry::parseManifest(
        QStringLiteral("{ this is not json"), &err).has_value());
    QVERIFY(err.contains(QStringLiteral("not valid JSON")));
}

void PluginRegistryTest::testParseRejectsMissingFields()
{
    // api_version absent → reject
    QString err;
    QVERIFY(!PluginRegistry::parseManifest(
        QStringLiteral(R"json({"id":"org.desktop-pet.x","version":"1.0"})json"),
        &err).has_value());
    QVERIFY(err.contains(QStringLiteral("api_version")));

    // id absent → reject
    QVERIFY(!PluginRegistry::parseManifest(
        QStringLiteral(R"json({"version":"1.0","api_version":"1.0"})json"),
        &err).has_value());
    QVERIFY(err.contains(QStringLiteral("id")));
}

void PluginRegistryTest::testParseRejectsBadIdShape()
{
    QString err;
    QVERIFY(!PluginRegistry::parseManifest(
        QStringLiteral(R"json({"id":"sample","version":"1.0","api_version":"1.0"})json"),
        &err).has_value());
    QVERIFY(err.contains(QStringLiteral("reverse-domain")));
}

void PluginRegistryTest::testParseRejectsNonNumericApiVersion()
{
    QString err;
    QVERIFY(!PluginRegistry::parseManifest(
        QStringLiteral(R"json({"id":"org.x.y","version":"1.0","api_version":"latest"})json"),
        &err).has_value());
    QVERIFY(err.contains(QStringLiteral("api_version")));
    // "1.10" must parse as major 1 minor 10 — never float-collapsed to 1.1
    const auto m = PluginRegistry::parseManifest(
        QStringLiteral(R"json({"id":"org.x.y","version":"1.0","api_version":"1.10"})json"));
    QVERIFY(m.has_value());
    QCOMPARE(m->apiMinor, 10);
}

void PluginRegistryTest::testApiVersionGating()
{
    QString err;
    // same version → ok; lower major/minor → ok
    QVERIFY(PluginRegistry::validateApiVersion(pet::kApiMajor, pet::kApiMinor));
    QVERIFY(PluginRegistry::validateApiVersion(pet::kApiMajor - 1, 99));
    QVERIFY(PluginRegistry::validateApiVersion(pet::kApiMajor, pet::kApiMinor - 1));
    // higher major → reject; same major higher minor → reject
    QVERIFY(!PluginRegistry::validateApiVersion(pet::kApiMajor + 1, 0, &err));
    QVERIFY(err.contains(QStringLiteral("exceeds")));
    QVERIFY(!PluginRegistry::validateApiVersion(pet::kApiMajor, pet::kApiMinor + 1, &err));
}

void PluginRegistryTest::testAbiGating()
{
    auto abi = [] (const char* compiler, const char* compilerVersion,
                   const char* qt, const char* build) {
        return QJsonObject{
            {"compiler", compiler},
            {"compiler_version", compilerVersion},
            {"qt_version", qt},
            {"qt_build", build},
            {"build_type", "Release"},
        };
    };
    const QJsonObject host = abi("GNU", "13.1.0", "6.10.2", "mingw_64");
    QString err;
    // identical → ok; different compiler → reject; version mismatch → reject;
    // build_type mismatch → reject; missing field → reject (fail closed)
    QVERIFY(PluginRegistry::validateAbi(host, host));
    QVERIFY(!PluginRegistry::validateAbi(abi("MSVC", "13.1.0", "6.10.2", "mingw_64"), host, &err));
    QVERIFY(err.contains(QStringLiteral("compiler")));
    QVERIFY(!PluginRegistry::validateAbi(abi("GNU", "13.2.0", "6.10.2", "mingw_64"), host, &err));
    QVERIFY(err.contains(QStringLiteral("compiler_version")));
    QVERIFY(!PluginRegistry::validateAbi(abi("GNU", "13.1.0", "6.10.2", "msvc_64"), host, &err));
    QVERIFY(err.contains(QStringLiteral("qt_build")));
    QJsonObject debugBuild = host;
    debugBuild["build_type"] = QStringLiteral("Debug");
    QVERIFY(!PluginRegistry::validateAbi(debugBuild, host, &err));
    QVERIFY(err.contains(QStringLiteral("build_type")));
    QJsonObject missing = host;
    missing.remove(QStringLiteral("compiler_version"));
    QVERIFY(!PluginRegistry::validateAbi(missing, host, &err));
}

void PluginRegistryTest::testAbiQtVersionRule()
{
    auto qt = [] (const char* v) {
        QJsonObject o{{"compiler", "GNU"}, {"compiler_version", "13.1.0"},
                      {"qt_build", "mingw_64"}, {"build_type", "Release"}};
        o["qt_version"] = QString::fromLatin1(v);
        return o;
    };
    const QJsonObject host = qt("6.10.2");
    QVERIFY(PluginRegistry::validateAbi(qt("6.10.0"), host));   // patch free
    QVERIFY(PluginRegistry::validateAbi(qt("6.9.5"), host));    // lower minor ok
    QVERIFY(!PluginRegistry::validateAbi(qt("6.11.0"), host));  // higher minor
    QVERIFY(!PluginRegistry::validateAbi(qt("7.0.0"), host));   // major bump
}

void PluginRegistryTest::testStateMachineLegalChain()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifest(), nullptr);
    const QString id = QStringLiteral("org.desktop-pet.sample");
    QCOMPARE(reg.entry(id)->status, PluginStatus::Registered); // compressed static path
    QVERIFY(reg.transition(id, PluginStatus::Started));
    QVERIFY(reg.transition(id, PluginStatus::Stopped));
    // Failed reachable from ANY state (from Stopped too — display semantics)
    QVERIFY(reg.transition(id, PluginStatus::Failed, QStringLiteral("boom")));
    QCOMPARE(reg.entry(id)->errorMessage, QStringLiteral("boom"));
}

void PluginRegistryTest::testStateMachineRejectsIllegalTransitions()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifest(), nullptr);
    const QString id = QStringLiteral("org.desktop-pet.sample");
    QVERIFY(!reg.transition(id, PluginStatus::Stopped));   // Registered→Stopped
    QVERIFY(!reg.transition(id, PluginStatus::Validated)); // Registered→Validated
    QVERIFY(reg.transition(id, PluginStatus::Started));
    QVERIFY(!reg.transition(id, PluginStatus::Started));   // Started→Started
    QVERIFY(reg.transition(id, PluginStatus::Stopped));
    QVERIFY(!reg.transition(id, PluginStatus::Started));   // terminal Stopped
    QCOMPARE(reg.entry(id)->status, PluginStatus::Stopped);
    QVERIFY(!reg.transition(QStringLiteral("no.such.plugin"), PluginStatus::Failed));
}

void PluginRegistryTest::testAddStaticPluginFailedEntryStaysVisible()
{
    PluginRegistry reg;
    reg.addStaticPlugin(QStringLiteral("{\"id\": \"org.desktop-pet.bad\""), nullptr);
    QCOMPARE(reg.size(), 1); // never silently dropped (§B.6)
    QCOMPARE(reg.entry(QStringLiteral("<invalid-manifest>"))->status,
             PluginStatus::Failed);
    QVERIFY(!reg.entry(QStringLiteral("<invalid-manifest>"))->errorMessage.isEmpty());
}

void PluginRegistryTest::testAddStaticPluginRejectsTooHighApiVersion()
{
    PluginRegistry reg;
    reg.addStaticPlugin(manifest(pet::kApiMajor, pet::kApiMinor + 5), nullptr);
    const auto entries = reg.orderedEntries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().status, PluginStatus::Failed);
    QVERIFY(entries.first().errorMessage.contains(QStringLiteral("exceeds")));
}

void PluginRegistryTest::testOrderedEntriesSortByManifestOrder()
{
    PluginRegistry reg;
    auto add = [&reg] (const char* id, int order) {
        const QString json = QStringLiteral(
            R"json({"id":"%1","version":"1.0","api_version":"1.0","order":%2})json")
            .arg(QLatin1String(id)).arg(order);
        reg.addStaticPlugin(json, nullptr);
    };
    add("org.desktop-pet.c", 200);
    add("org.desktop-pet.b", 100);
    add("org.desktop-pet.a", 100); // tie → id asc
    const auto ordered = reg.orderedEntries();
    QCOMPARE(ordered.at(0).manifest.id, QStringLiteral("org.desktop-pet.a"));
    QCOMPARE(ordered.at(1).manifest.id, QStringLiteral("org.desktop-pet.b"));
    QCOMPARE(ordered.at(2).manifest.id, QStringLiteral("org.desktop-pet.c"));
}

QTEST_APPLESS_MAIN(PluginRegistryTest)
#include "PluginRegistryTest.moc"
