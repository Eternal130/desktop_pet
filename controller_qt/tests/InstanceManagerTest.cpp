// InstanceManagerTest (Phase 5, todo 3) — the sidebar QAbstractListModel.
//
// Four behaviors locked (matches the task spec MUST DO list):
//   1. testCreateAndPersist: create 2 instances → rowCount==2, both UUIDs in
//      PanelConfig.instanceIds IN CREATION ORDER (panel.json round-trip), the
//      instance files exist, out-of-range instanceAt → nullptr.
//   2. testDelete: delete one → rowCount==1, file gone, panel.json updated
//      (remaining UUID preserved in order), deleting an unknown uuid is a
//      safe no-op, requestDelete emits deleteConfirmed.
//   3. testCorruptInstanceDegrades: a valid panel.json listing one valid +
//      one corrupt instance file → InstanceManager loads only the valid one
//      (WARN-skip on the corrupt), never crashes.
//   4. testRoute: route a response to session 2's instanceId → the shared
//      PendingRequests resolves it (route → onMessage → dispatch →
//      handleResponse). Routing an unknown instanceId is a safe no-op (WARN).
//
// QTEST_MAIN (NOT APPLESS): route() touches InstanceSession/WsServer, and
// QSignalSpy over PendingRequests::resolved is safest with a real application.
// No renderer is launched (createInstance never calls start()), so no
// REQUIRES_RENDERER label — the WsServer is constructed but does not listen.
//
// savePanel injection (m5 fix): every test passes a capturing lambda that
// writes via PanelStateManager into the QTemporaryDir, mimicking main.cpp's
// production wiring. InstanceManager itself never depends on PanelStateManager.

#include "core/DatabaseManager.hpp"
#include "core/InstanceConfig.hpp"
#include "core/InstanceConfigManager.hpp"
#include "core/InstanceManager.hpp"
#include "core/InstanceSession.hpp"
#include "core/PanelConfig.hpp"
#include "core/PanelStateManager.hpp"
#include "network/Envelope.hpp"
#include "network/PendingRequests.hpp"
#include "network/WsServer.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Write `data` to `<base>/<name>`, creating parent dirs as needed. Used to
// plant a corrupt instance fixture directly into the temp dir.
void writeFile(const QString& base, const QString& name, const QByteArray& data)
{
    QDir().mkpath(base);
    QFile f(base + QLatin1Char('/') + name);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("Failed to write %1: %2")
                            .arg(f.fileName(), f.errorString())));
    f.write(data);
    f.close();
}

// Absolute path of the instance file <base>/instances/<uuid>.json.
QString instanceFile(const QString& base, const QString& uuid)
{
    return base + QStringLiteral("/instances/") + uuid + QStringLiteral(".json");
}

// S4 headless harness: a fake renderer DIRECTORY whose "desktop-pet-renderer"
// is really a 30s sleeper script (existence is all resolveRendererPath
// checks; shebang + exec bit make QProcess run it). POSIX-only — call sites
// QSKIP on Windows. Out-param — QVERIFY2 needs a void return.
void makeFakeRendererDir(const QString& base, QString* outDir)
{
    const QString rendererDir =
        QDir(base).absoluteFilePath(QStringLiteral("renderer"));
    QDir().mkpath(rendererDir);
    const QString exe =
        QDir(rendererDir).absoluteFilePath(QStringLiteral("desktop-pet-renderer"));
    QFile f(exe);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(QStringLiteral("Failed to create %1").arg(exe)));
    f.write(QStringLiteral("#!/bin/sh\nexec sleep 30\n").toUtf8());
    f.close();
    QVERIFY2(QFile::setPermissions(exe,
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner
                                       | QFileDevice::ReadGroup
                                       | QFileDevice::ExeGroup
                                       | QFileDevice::ReadOther
                                       | QFileDevice::ExeOther),
             qPrintable(QStringLiteral("Failed to chmod %1").arg(exe)));
    *outDir = rendererDir;
}

} // namespace

class InstanceManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void testCreateAndPersist();
    void testCreateWithModelName();
    void testDelete();
    void testCorruptInstanceDegrades();
    void testRoute();
    void testSetDatabaseDoesNotDuplicateRoster();

    // ── S4 two-phase delete / async stopAll (fake-sleeper renderer; POSIX) ──
    void testDeleteRunningInstanceIsTwoPhase();
    void testStopAllIsAsync();
};

void InstanceManagerTest::initTestCase()
{
    // PendingResult is carried by PendingRequests::resolved; register it so
    // QSignalSpy can capture the (id, result) pair without a qWarning.
    qRegisterMetaType<PendingResult>("PendingResult");
}

// ────────────────────────────────────────────────────────────────────────────
// 1. create + persist (creation-order preserved through panel.json round-trip)
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testCreateAndPersist()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    WsServer server;
    PendingRequests pending;

    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });

    QCOMPARE(mgr.rowCount(), 0);

    const QString uuid1 = mgr.createInstance(QStringLiteral("Pet One"));
    const QString uuid2 = mgr.createInstance(QStringLiteral("Pet Two"));

    QVERIFY2(!uuid1.isEmpty() && !uuid2.isEmpty(),
             "createInstance returned an empty uuid (persist failed)");
    QVERIFY(uuid1 != uuid2);
    QCOMPARE(mgr.rowCount(), 2);

    // Sidebar order = creation order.
    QCOMPARE(mgr.instanceAt(0)->label(), QStringLiteral("Pet One"));
    QCOMPARE(mgr.instanceAt(1)->label(), QStringLiteral("Pet Two"));
    QCOMPARE(mgr.instanceAt(0)->config().id, uuid1);
    QCOMPARE(mgr.instanceAt(1)->config().id, uuid2);

    // instanceIds persisted in creation order — re-read panel.json from disk.
    const PanelConfig reloaded = PanelStateManager(base.path()).load();
    QCOMPARE(reloaded.instanceIds.size(), 2);
    QCOMPARE(reloaded.instanceIds.at(0), uuid1);
    QCOMPARE(reloaded.instanceIds.at(1), uuid2);

    // The instance rows exist in the SQLite db (SQLite backend — no files).
    {
        DatabaseManager db;
        QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
        QVERIFY2(db.loadInstance(uuid1).has_value(),
                 "instance row for uuid1 not created");
        QVERIFY2(db.loadInstance(uuid2).has_value(),
                 "instance row for uuid2 not created");
    }

    // Out-of-range instanceAt → nullptr (never derefs a bad index).
    QVERIFY(mgr.instanceAt(-1) == nullptr);
    QVERIFY(mgr.instanceAt(99) == nullptr);
}

// ────────────────────────────────────────────────────────────────────────────
// 1b. 模型库 B 档: createInstance with an explicit modelName
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testCreateWithModelName()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    WsServer server;
    PendingRequests pending;

    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });

    // Third parameter carries the model-library selection into the config.
    const QString uuid = mgr.createInstance(QStringLiteral("Mao Pet"),
                                            QStringLiteral("🐱"),
                                            QStringLiteral("Mao"));
    QVERIFY2(!uuid.isEmpty(), "createInstance returned an empty uuid");

    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(mgr.instanceAt(0)->modelName(), QStringLiteral("Mao"));
    QCOMPARE(mgr.instanceAt(0)->config().modelName, QStringLiteral("Mao"));

    // Persisted with the chosen model — a fresh load reads it back.
    {
        DatabaseManager db;
        QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
        const auto row = db.loadInstance(uuid);
        QVERIFY2(row.has_value(), "instance row not created");
        QCOMPARE(row->modelName, QStringLiteral("Mao"));
    }

    // The 2-arg form (pre-parameter call sites) keeps the default model —
    // the default-argument compatibility guarantee.
    const QString defaultUuid = mgr.createInstance(QStringLiteral("Default Pet"));
    QVERIFY2(!defaultUuid.isEmpty(), "2-arg createInstance returned an empty uuid");
    QCOMPARE(mgr.instanceAt(1)->config().modelName,
             defaultInstanceConfig().modelName);
}

// ────────────────────────────────────────────────────────────────────────────
// 2. delete (file gone + panel.json updated + safe no-op + deleteConfirmed)
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testDelete()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });

    const QString uuid1 = mgr.createInstance(QStringLiteral("Keep"));
    const QString uuid2 = mgr.createInstance(QStringLiteral("Delete"));
    QCOMPARE(mgr.rowCount(), 2);

    mgr.deleteInstance(uuid2);
    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(mgr.instanceAt(0)->label(), QStringLiteral("Keep"));
    QCOMPARE(mgr.instanceAt(0)->config().id, uuid1);

    // File gone.
    QVERIFY2(!QFile::exists(instanceFile(base.path(), uuid2)),
             "instance file for deleted uuid should be removed");

    // panel.json updated: uuid2 removed, uuid1 preserved in order.
    const PanelConfig reloaded = PanelStateManager(base.path()).load();
    QCOMPARE(reloaded.instanceIds.size(), 1);
    QCOMPARE(reloaded.instanceIds.at(0), uuid1);

    // Deleting an unknown uuid → safe no-op (WARN log, no crash, no row change).
    mgr.deleteInstance(QStringLiteral("nonexistent-uuid-0000"));
    QCOMPARE(mgr.rowCount(), 1);

    // requestDelete emits deleteConfirmed (the delete-protection pattern —
    // blueprint §4.2). The UI wires this to a confirm dialog; it must NOT touch
    // the roster itself.
    QSignalSpy confirmSpy(&mgr, &InstanceManager::deleteConfirmed);
    QVERIFY(confirmSpy.isValid());
    QCOMPARE(mgr.rowCount(), 1);
    mgr.requestDelete(uuid1);
    QCOMPARE(confirmSpy.count(), 1);
    QCOMPARE(confirmSpy.at(0).at(0).toString(), uuid1);
    // requestDelete did not delete — roster unchanged.
    QCOMPARE(mgr.rowCount(), 1);
}

// ────────────────────────────────────────────────────────────────────────────
// 3. corrupt-degradation (skip the bad file with WARN, load the good one)
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testCorruptInstanceDegrades()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Write ONE valid instance + ONE corrupt instance, and a panel.json that
    // lists BOTH in instanceIds (valid first, corrupt second). The m4 order is
    // preserved: InstanceManager iterates instanceIds, not loadAll's sort.
    InstanceConfig valid = defaultInstanceConfig();
    valid.label = QStringLiteral("Good");
    QVERIFY(InstanceConfigManager(base.path()).save(valid));

    const QString corruptId = QStringLiteral("deadbeef-dead-dead-dead-deadbeefdead");
    writeFile(base.path() + QStringLiteral("/instances"),
              corruptId + QStringLiteral(".json"),
              QByteArray("{ this is not valid json"));

    PanelConfig panel = defaultPanelConfig();
    panel.instanceIds = QStringList{valid.id, corruptId};
    QVERIFY(PanelStateManager(base.path()).save(panel));

    // Construct InstanceManager — it must skip the corrupt file (WARN) and load
    // only the valid one, without crashing. The corrupt id stays in instanceIds
    // until the next persistRoster() (no auto-cleanup at load — deliberate, the
    // spec asks for degradation not self-repair at construction).
    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });

    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(mgr.instanceAt(0)->label(), QStringLiteral("Good"));
    QCOMPARE(mgr.instanceAt(0)->config().id, valid.id);
}

// ────────────────────────────────────────────────────────────────────────────
// 4. route (demux to the session whose instanceId matches; unknown → no-op)
// ────────────────────────────────────────────────────────────────────────────
//
// route() forwards env to InstanceSession::onMessage, which dispatches. Every
// session's dispatcher has its response handler wired in the InstanceSession
// constructor (NOT gated on start()), so a type=="response" envelope reaches
// the shared PendingRequests table synchronously (same-thread AutoConnection =
// DirectConnection). The resolved(id, result) signal therefore fires inline.
//
// Note on per-session demux: PendingRequests is shared across instances (M2 —
// the id→result table is instance-agnostic), so a response routed to ANY
// session resolves by envelope id. This test therefore proves route forwards
// to onMessage for a matched instanceId and is a safe no-op for an unknown one;
// the linear-scan match by instanceId() is structurally correct (a simple loop).

void InstanceManagerTest::testRoute()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    WsServer server;
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });

    mgr.createInstance(QStringLiteral("A"));
    mgr.createInstance(QStringLiteral("B"));
    auto* s1 = mgr.instanceAt(0);
    auto* s2 = mgr.instanceAt(1);
    QVERIFY(s1 != nullptr);
    QVERIFY(s2 != nullptr);
    const int id1 = s1->instanceId();
    const int id2 = s2->instanceId();
    QVERIFY2(id1 != id2, "two distinct UUIDs hashed to the same instanceId");

    // Register a pending request "k1", then route a matching response to
    // session 2's instanceId. route → s2.onMessage → dispatch → handleResponse
    // resolves the pending request synchronously.
    pending.expectResponse(QStringLiteral("k1"));
    QSignalSpy resolvedSpy(&pending, &PendingRequests::resolved);
    QVERIFY(resolvedSpy.isValid());

    const Envelope resp = createResponse(
        QStringLiteral("k1"), QStringLiteral("get_state"), /*success=*/true);
    mgr.route(id2, resp);

    QCOMPARE(resolvedSpy.count(), 1);
    QCOMPARE(resolvedSpy.at(0).at(0).toString(), QStringLiteral("k1"));

    // Routing to an UNKNOWN instanceId must not resolve anything (WARN log) and
    // must not crash. Use an instanceId neither session can have: a negative
    // value is impossible (InstanceSession masks qHash with 0x7FFFFFFF).
    mgr.route(-1, resp);
    QCOMPARE(resolvedSpy.count(), 1); // unchanged — no spurious resolution
}

// ────────────────────────────────────────────────────────────────────────────
// 5. setDatabase() after construction must not duplicate the roster.
// Regression: the ctor runs loadFromDisk() once (own lazily-opened db), then
// main.cpp calls setDatabase() which loads again — without clearing the
// roster first, every persisted instance appears TWICE on restart.
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testSetDatabaseDoesNotDuplicateRoster()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Seed: one instance + roster persisted in <base>/app.db (SQLite backend).
    {
        WsServer server;
        PendingRequests pending;
        DatabaseManager db;
        QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
        InstanceManager mgr(base.path(), server, pending,
                            [&base](const PanelConfig& cfg) {
                                PanelStateManager(base.path()).save(cfg);
                            });
        mgr.setDatabase(&db);
        QVERIFY(!mgr.createInstance(QStringLiteral("Solo")).isEmpty());
    }

    // Replay main.cpp's wiring: construct (loads once), then setDatabase.
    WsServer server;
    PendingRequests pending;
    DatabaseManager db;
    QVERIFY(db.open(base.path() + QStringLiteral("/app.db")));
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& cfg) {
                            PanelStateManager(base.path()).save(cfg);
                        });
    const int before = mgr.rowCount();
    QCOMPARE(before, 1);
    mgr.setDatabase(&db);
    QCOMPARE(mgr.rowCount(), 1);
    QCOMPARE(mgr.instanceAt(0)->label(), QStringLiteral("Solo"));
}

// ────────────────────────────────────────────────────────────────────────────
// 6. S4 two-phase delete of a RUNNING instance (fake-sleeper renderer).
// Phase 1: deleteInstance returns immediately — row STAYS, no premature
// rowsAboutToBeRemoved, start() on the doomed session is rejected, a second
// deleteInstance merges (no-op). Phase 2: once the async stop completes the
// row is really removed (rowsAboutToBeRemoved fires exactly once, at the
// removal), the config is gone, the roster persisted.
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testDeleteRunningInstanceIsTwoPhase()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Seed one instance whose rendererPath points at the fake renderer dir.
    InstanceConfig cfg = defaultInstanceConfig();
    cfg.label = QStringLiteral("Runner");
    QString rendererDir;
    makeFakeRendererDir(base.path(), &rendererDir);
    cfg.rendererPath = rendererDir;
    cfg.graphicsBackend = QStringLiteral("opengl");
    QVERIFY(InstanceConfigManager(base.path()).save(cfg));
    PanelConfig panel = defaultPanelConfig();
    panel.instanceIds = QStringList{cfg.id};
    QVERIFY(PanelStateManager(base.path()).save(panel));

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& p) {
                            PanelStateManager(base.path()).save(p);
                        });

    QCOMPARE(mgr.rowCount(), 1);
    InstanceSession* session = mgr.instanceAt(0);
    QVERIFY(session != nullptr);
    session->setStopTimeoutMs(300); // fast kill for the winding-down phase

    session->start();
    QVERIFY2(session->isProcessRunning(), "fake renderer process failed to launch");

    QSignalSpy aboutSpy(&mgr, &QAbstractItemModel::rowsAboutToBeRemoved);
    QVERIFY(aboutSpy.isValid());

    // ── Phase 1: non-blocking initiate; nothing removed yet ───────────────
    mgr.deleteInstance(cfg.id);
    QCOMPARE(mgr.rowCount(), 1);          // row survives until the stop lands
    QCOMPARE(aboutSpy.count(), 0);        // no premature rowsAboutToBeRemoved
    QVERIFY2(session->isProcessRunning(),
             "deleteInstance must not block on the running renderer");

    // start() on the doomed session is rejected (no relaunch race).
    session->start();
    QCOMPARE(session->status(), QStringLiteral("stopped")); // unchanged by the rejected start

    // Re-delete while pending merges — safe, no double removal.
    mgr.deleteInstance(cfg.id);
    QCOMPARE(mgr.rowCount(), 1);

    // ── Phase 2: the async stop completes → REAL removal ──────────────────
    QTRY_COMPARE_WITH_TIMEOUT(mgr.rowCount(), 0, 5000);
    QCOMPARE(aboutSpy.count(), 1);        // fired exactly once, at the removal
    QVERIFY(mgr.instanceAt(0) == nullptr);

    // Config row + roster cleaned up.
    QVERIFY2(!InstanceConfigManager(base.path()).load(cfg.id).has_value(),
             "instance config survived the deferred delete");
    const PanelConfig reloaded = PanelStateManager(base.path()).load();
    QCOMPARE(reloaded.instanceIds.size(), 0);

    server.close();
#endif
}

// ────────────────────────────────────────────────────────────────────────────
// 7. S4 stopAll is event-driven: returns the number of winding-down
// renderers while they are STILL alive, emits stopAllFinished when the last
// one is down, removes no rows, and returns 0 (emitting nothing) when
// nothing runs.
// ────────────────────────────────────────────────────────────────────────────

void InstanceManagerTest::testStopAllIsAsync()
{
#ifdef Q_OS_WIN
    QSKIP("fake-renderer sleeper harness is POSIX-only");
#else
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Seed two instances on the fake renderer.
    QString rendererDir;
    makeFakeRendererDir(base.path(), &rendererDir);
    QStringList uuids;
    for (int i = 0; i < 2; ++i) {
        InstanceConfig cfg = defaultInstanceConfig();
        cfg.label = QStringLiteral("Runner %1").arg(i + 1);
        cfg.rendererPath = rendererDir;
        cfg.graphicsBackend = QStringLiteral("opengl");
        QVERIFY(InstanceConfigManager(base.path()).save(cfg));
        uuids.append(cfg.id);
    }
    PanelConfig panel = defaultPanelConfig();
    panel.instanceIds = uuids;
    QVERIFY(PanelStateManager(base.path()).save(panel));

    WsServer server;
    QVERIFY2(server.listen(0), "WsServer failed to listen on port 0");
    PendingRequests pending;
    InstanceManager mgr(base.path(), server, pending,
                        [&base](const PanelConfig& p) {
                            PanelStateManager(base.path()).save(p);
                        });

    QCOMPARE(mgr.rowCount(), 2);
    for (int i = 0; i < 2; ++i) {
        InstanceSession* session = mgr.instanceAt(i);
        QVERIFY(session != nullptr);
        session->setStopTimeoutMs(300);
        session->start();
        QVERIFY2(session->isProcessRunning(),
                 qPrintable(QStringLiteral("fake renderer %1 failed to launch").arg(i)));
    }

    QSignalSpy allSpy(&mgr, &InstanceManager::stopAllFinished);
    QVERIFY(allSpy.isValid());

    const int initiated = mgr.stopAll();
    QCOMPARE(initiated, 2);

    // Non-blocking proof: both renderers still winding down on return.
    QVERIFY2(mgr.instanceAt(0)->isProcessRunning(),
             "stopAll must return before the renderers exit");
    QVERIFY2(mgr.instanceAt(1)->isProcessRunning(),
             "stopAll must return before the renderers exit");

    QVERIFY2(allSpy.wait(5000), "stopAllFinished not emitted within 5s");
    QVERIFY2(!mgr.instanceAt(0)->isProcessRunning(), "renderer 0 still alive");
    QVERIFY2(!mgr.instanceAt(1)->isProcessRunning(), "renderer 1 still alive");
    QCOMPARE(mgr.rowCount(), 2); // stop ≠ delete — rows intact

    // Nothing running → stopAll initiates nothing and emits nothing.
    QCOMPARE(mgr.stopAll(), 0);
    QCOMPARE(allSpy.count(), 1); // unchanged

    server.close();
#endif
}

QTEST_MAIN(InstanceManagerTest)

#include "InstanceManagerTest.moc"
