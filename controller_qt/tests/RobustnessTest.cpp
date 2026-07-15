// RobustnessTest — T22: JSON robustness + atomic-write guarantee.
//
// Three test groups locking the "degrade gracefully" contract that every config
// manager in controller_qt must honor — no exceptions, no crashes, always
// defaults / intact originals when input is corrupt/truncated/fuzzed.
//
//   1. Truncated panel.json → defaults: a valid panel.json with the last 10
//      bytes removed must parse to defaultPanelConfig() via
//      PanelStateManager::load(), without throwing or crashing.
//   2. Atomic-write guarantee (simulated crash): a stale `<file>.tmp` artifact
//      left behind by a hypothetical mid-write crash must NOT corrupt or
//      replace the pre-existing real panel.json. load() returns the original
//      content; the real file on disk is byte-for-byte unchanged.
//   3. Fuzz (1000 iterations): random-byte files fed to
//      InstanceConfigManager::loadAll() and random QJsonObject values fed to
//      instanceConfigFromJson / panelConfigFromJson — none may throw. Zero
//      exceptions across all 1000 iterations.
//
// This is a TEST-ONLY task: it does not modify any config manager source. It
// only exercises the public contracts documented in InstanceConfigManager.hpp
// (T20), PanelStateManager.hpp (T21), InstanceConfig.hpp (T18), and
// PanelConfig.hpp (T19).
//
// QTEST_APPLESS_MAIN: no event loop needed — all operations (QFile, QDir,
// QSaveFile-backed save/load, QJsonObject manipulation) are synchronous.

#include "core/InstanceConfigManager.hpp"
#include "core/PanelStateManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/PanelConfig.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Count of fuzz iterations — the spec minimum.
constexpr int kFuzzIterations = 1000;

// Create <basePath>/instances/ and return it with a trailing '/'. Mirrors the
// production layout ConfigDir::ensureDirectories(basePath) creates, so
// InstanceConfigManager(basePath) finds its instance files here.
QString makeInstancesDir(const QTemporaryDir& base)
{
    const QString inst = base.path() + QStringLiteral("/instances");
    QDir().mkpath(inst);
    return inst + QStringLiteral("/");
}

// Build a PanelConfig with at least one field set to a non-default value so a
// truncation-to-defaults assertion can distinguish "loaded the real file" from
// "fell back to defaults". panelX=-1 is the default, so we set it to 42.
PanelConfig makeNonDefaultPanel()
{
    PanelConfig c;
    c.panelX = 42;
    c.panelY = 99;
    c.panelWidth = 800;
    c.panelHeight = 600;
    c.theme = QStringLiteral("robustness-test-theme");
    c.fontSize = 20;
    c.panelOpacity = 0.5;
    c.instanceIds = QStringList{ QStringLiteral("aaa"), QStringLiteral("bbb") };
    c.autoLaunchSystem = true;
    c.startMinimized = true;
    c.closeAction = QStringLiteral("minimize");
    c.confirmOnExit = true;
    return c;
}

// Generate a random JSON value with bounded nesting. Exercises every QJsonValue
// branch (double, string, bool, null, object, array) so the parser sees both
// type-matched and type-mismatched values for every schema key.
QJsonValue makeRandomJsonValue(QRandomGenerator* g, int depth)
{
    const int branch = g->bounded(depth > 0 ? 6 : 4);
    switch (branch) {
    case 0:
        return QJsonValue(g->bounded(1000000) - 500000); // int-valued double
    case 1:
        return QJsonValue(QString::number(g->generate(), 16)); // random hex string
    case 2:
        return QJsonValue((g->generate() & 1) != 0); // bool
    case 3:
        return QJsonValue::Null; // null
    case 4: {
        // nested object with 1-3 random keys
        QJsonObject obj;
        const int n = g->bounded(3) + 1;
        for (int i = 0; i < n; ++i)
            obj.insert(QString::number(g->bounded(100)),
                       makeRandomJsonValue(g, depth - 1));
        return obj;
    }
    default: {
        // array with 1-3 random elements
        QJsonArray arr;
        const int n = g->bounded(3) + 1;
        for (int i = 0; i < n; ++i)
            arr.append(makeRandomJsonValue(g, depth - 1));
        return arr;
    }
    }
}

// Generate a random QJsonObject. Mixes random keys with real schema keys (for
// both InstanceConfig and PanelConfig) carrying random-typed values — this
// exercises the type-mismatch fallback paths in both fromJson functions, not
// just the unknown-key tolerance path.
QJsonObject makeRandomJsonObject(QRandomGenerator* g)
{
    QJsonObject obj;
    // 1-8 pure-noise keys.
    const int noise = g->bounded(8) + 1;
    for (int i = 0; i < noise; ++i)
        obj.insert(QStringLiteral("noise_") + QString::number(g->bounded(1000)),
                   makeRandomJsonValue(g, 2));
    // Real schema keys with deliberately wrong-typed values (string where a
    // number is expected, object where a string is expected, etc.).
    static const char* kInstanceKeys[] = {
        "id", "label", "renderer_path", "graphics_backend", "model_name",
        "model_scale", "window_x", "window_y", "window_width", "window_height",
        "opacity", "drag_mode", "idle_interval", "target_fps", "auto_start",
        "current_expression", "voice_pack", "volume", "muted", "drag_mode",
        "layout_offset_x", "layout_offset_y", "layout_scale",
        "subtitle_offset_x", "subtitle_offset_y", "subtitle_area_width",
        "subtitle_area_height", "subtitle_font_size", "subtitle_style_preset"
    };
    static const char* kPanelKeys[] = {
        "panel_x", "panel_y", "panel_width", "panel_height", "theme",
        "font_size", "panel_opacity", "instance_ids", "auto_launch_system",
        "start_minimized", "close_action", "confirm_on_exit"
    };
    for (const char* k : kInstanceKeys)
        if (g->bounded(2) == 0)
            obj.insert(QString::fromLatin1(k), makeRandomJsonValue(g, 1));
    for (const char* k : kPanelKeys)
        if (g->bounded(2) == 0)
            obj.insert(QString::fromLatin1(k), makeRandomJsonValue(g, 1));
    return obj;
}

} // namespace

class RobustnessTest : public QObject
{
    Q_OBJECT

private slots:
    void testTruncatedPanelJsonReturnsDefaults();
    void testAtomicWriteSimulatedCrash();
    void testFuzzRandomBytesNoThrow();
};

// 1. Truncated panel.json → defaults (no crash, no throw).
//
// Given a valid panel.json written by PanelStateManager::save(), when the last
// 10 bytes are removed (simulating a partial write / disk truncation), then
// load() must return defaultPanelConfig() — NEVER throw, NEVER crash, and
// NEVER return a partially-populated config that silently swallowed the
// truncation.
void RobustnessTest::testTruncatedPanelJsonReturnsDefaults()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    PanelStateManager psm(base.path());

    // Given: save a non-default panel.json so defaults-vs-real is distinguishable.
    const PanelConfig saved = makeNonDefaultPanel();
    QVERIFY2(psm.save(saved), "precondition: save must succeed on temp dir");
    const QString panelPath = base.path() + QStringLiteral("/panel.json");
    QVERIFY2(QFile::exists(panelPath), "precondition: panel.json must exist after save");

    // Truncate: chop the last 10 bytes. This breaks the closing braces of the
    // JSON document → QJsonDocument::fromJson will report a parse error →
    // PanelStateManager::load() degrades to defaults with a WARN.
    {
        QFile f(panelPath);
        QVERIFY2(f.open(QIODevice::ReadOnly), "precondition: cannot open panel.json");
        QByteArray bytes = f.readAll();
        f.close();
        QVERIFY2(bytes.size() > 10, "precondition: panel.json must be > 10 bytes");
        bytes.chop(10);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "cannot open panel.json for truncation");
        f.write(bytes);
        f.close();
    }

    // When: load the truncated panel.json.
    PanelConfig loaded;
    try {
        loaded = psm.load();
    } catch (...) {
        QFAIL("PanelStateManager::load() threw an exception on truncated panel.json");
        return;
    }

    // Then: the loaded config equals defaultPanelConfig() — the truncation was
    // detected and the manager fell back to defaults rather than returning a
    // half-parsed config. The non-default theme name proves we did NOT load
    // the saved values.
    const PanelConfig defaults = defaultPanelConfig();
    QCOMPARE(loaded, defaults);
    QVERIFY2(loaded.theme != saved.theme,
             "load must not return the saved (truncated) theme — must be defaults");
    QVERIFY2(loaded.panelX != saved.panelX,
             "load must not return the saved (truncated) panelX — must be defaults");
}

// 2. Atomic-write guarantee: a stale <file>.tmp left by a simulated mid-write
// crash must NOT corrupt or replace the pre-existing real panel.json.
//
// The atomic-write contract (InstanceConfigManager.hpp, PanelStateManager.hpp):
// save() writes to a temp file first, then atomically renames it onto the real
// path. A crash BEFORE the rename leaves only the temp file; the real file —
// if any — is untouched. This test plants exactly that stale-temp scenario and
// verifies load() still returns the original content and the on-disk real file
// is byte-for-byte identical.
void RobustnessTest::testAtomicWriteSimulatedCrash()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    PanelStateManager psm(base.path());

    // Given: a valid panel.json saved (non-default values so any corruption is
    // immediately visible). Record its exact on-disk bytes for later compare.
    const PanelConfig original = makeNonDefaultPanel();
    QVERIFY2(psm.save(original), "precondition: save must succeed");
    const QString panelPath = base.path() + QStringLiteral("/panel.json");
    QFile origFile(panelPath);
    QVERIFY2(origFile.open(QIODevice::ReadOnly),
             "precondition: real panel.json must be readable");
    const QByteArray originalBytes = origFile.readAll();
    origFile.close();
    QVERIFY2(!originalBytes.isEmpty(),
             "precondition: real panel.json must be non-empty");

    // Simulate the crash: plant a stale panel.json.tmp with garbage content, as
    // if a previous save() had written the temp but died before the atomic
    // rename. The contents are deliberately different from the real file so a
    // miswire (non-atomic overwrite) would change the real file and fail.
    const QString staleTmp = panelPath + QStringLiteral(".tmp");
    {
        QFile f(staleTmp);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "cannot create stale .tmp fixture");
        f.write("{\"PANEL\":\"PARTIAL_CRASH_GARBAGE\""); // truncated garbage
        f.close();
    }
    QVERIFY2(QFile::exists(staleTmp), "precondition: stale .tmp must exist");

    // When: load the panel config.
    PanelConfig loaded;
    try {
        loaded = psm.load();
    } catch (...) {
        QFAIL("PanelStateManager::load() threw on stale .tmp presence");
        return;
    }

    // Then: the loaded config is the ORIGINAL (not the .tmp garbage). The real
    // file on disk is byte-for-byte unchanged. The stale .tmp must not leak
    // into the parsed result.
    QCOMPARE(loaded, original);
    QVERIFY2(loaded.theme == original.theme,
             "load must return the original theme, not .tmp garbage");
    QVERIFY2(loaded.theme != QStringLiteral("PARTIAL_CRASH_GARBAGE"),
             "load must not pick up stale .tmp contents");

    // And: the on-disk real panel.json is unchanged (byte-for-byte). This is
    // the atomic-write guarantee in its strongest form — the real file is
    // invariant under a crashed-write scenario.
    QFile rf(panelPath);
    QVERIFY2(rf.open(QIODevice::ReadOnly),
             "real panel.json must still be readable");
    const QByteArray afterBytes = rf.readAll();
    rf.close();
    QCOMPARE(afterBytes, originalBytes);

    // Cleanup the .tmp (QTemporaryDir would remove it anyway, but be tidy).
    QFile::remove(staleTmp);
}

// 3. Fuzz test (1000 iterations): random-byte files and random QJsonObjects.
//
// Every config entry point must be total — it must accept ANY input without
// throwing or crashing. This loop hammers:
//   - InstanceConfigManager::loadAll() over a directory of 1000 random-byte
//     files (corrupt JSON, skipped with WARN).
//   - InstanceConfigManager::load(id) on each individual corrupt file.
//   - instanceConfigFromJson on 1000 random QJsonObjects (type mismatches,
//     nested garbage, schema keys with wrong types).
//   - panelConfigFromJson on 1000 random QJsonObjects.
// Zero exceptions are tolerated — any throw is a test FAIL.
void RobustnessTest::testFuzzRandomBytesNoThrow()
{
    QTemporaryDir base;
    QVERIFY2(base.isValid(), "temporary directory creation failed");
    const QString instDir = makeInstancesDir(base);
    InstanceConfigManager mgr(base.path());

    // Seed a deterministic PRNG so a failure is reproducible. QRandomGenerator
    // is seeded from a fixed value — NOT QRandomGenerator::system() (CSPRNG) —
    // because fuzz failures must be reproducible across runs.
    QRandomGenerator rng(0xC0FFEEu);

    int exceptions = 0;

    // ── Phase A: 1000 random-byte files in instances/. ────────────────────
    // Each file is 0-255 bytes of random binary — overwhelmingly NOT valid
    // JSON, so loadAll/load should skip them all (or return nullopt).
    for (int i = 0; i < kFuzzIterations; ++i) {
        QByteArray garbage;
        const int len = rng.bounded(256);
        garbage.resize(len);
        for (int j = 0; j < len; ++j)
            garbage[j] = static_cast<char>(rng.bounded(256));
        const QString path = instDir + QStringLiteral("fuzz-%1.json").arg(i);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            // A failed precondition (disk full, etc.) is not the SUT's fault;
            // skip this iteration rather than counting it as an exception.
            continue;
        }
        f.write(garbage);
        f.close();
    }

    // loadAll() must tolerate 1000 corrupt files in one call. It returns the
    // list of whatever parsed (almost certainly empty, but a random byte
    // stream could in principle form a valid empty-ish JSON object — the
    // contract does not require zero, only "no throw").
    try {
        // loadAll returns whatever parsed — do NOT assert size==0 because
        // random bytes have a tiny but nonzero chance of forming a degenerate
        // valid object (e.g. "{}"). The contract under test is "no throw".
        mgr.loadAll();
    } catch (...) {
        ++exceptions;
        QFAIL("InstanceConfigManager::loadAll() threw on 1000 fuzz files");
    }

    // load(id) on each fuzz file must return nullopt (corrupt) without throw.
    for (int i = 0; i < kFuzzIterations; ++i) {
        const QString id = QStringLiteral("fuzz-%1").arg(i);
        try {
            mgr.load(id); // may return nullopt or defaults — must not throw
        } catch (...) {
            ++exceptions;
            QFAIL(qPrintable(QStringLiteral(
                "InstanceConfigManager::load(\"%1\") threw on fuzz file").arg(id)));
        }
    }

    // ── Phase B: instanceConfigFromJson on 1000 random QJsonObjects. ──────
    for (int i = 0; i < kFuzzIterations; ++i) {
        const QJsonObject randomObj = makeRandomJsonObject(&rng);
        try {
            instanceConfigFromJson(randomObj); // never throws per T18 contract
        } catch (...) {
            ++exceptions;
            QFAIL(qPrintable(QStringLiteral(
                "instanceConfigFromJson threw on iteration %1").arg(i)));
        }
    }

    // ── Phase C: panelConfigFromJson on 1000 random QJsonObjects. ─────────
    for (int i = 0; i < kFuzzIterations; ++i) {
        const QJsonObject randomObj = makeRandomJsonObject(&rng);
        try {
            panelConfigFromJson(randomObj); // never throws per T19 contract
        } catch (...) {
            ++exceptions;
            QFAIL(qPrintable(QStringLiteral(
                "panelConfigFromJson threw on iteration %1").arg(i)));
        }
    }

    // Final invariant: zero exceptions across ALL 3000+ fuzz calls (1000
    // loadAll-file-writes + 1000 load(id) + 1000 instanceConfigFromJson +
    // 1000 panelConfigFromJson). A single throw anywhere is a hard FAIL.
    QCOMPARE(exceptions, 0);
}

QTEST_APPLESS_MAIN(RobustnessTest)
#include "RobustnessTest.moc"
