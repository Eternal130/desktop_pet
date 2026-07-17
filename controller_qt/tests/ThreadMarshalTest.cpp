#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QThread>
#include <QUrl>
#include <QWebSocket>
#include <QNetworkRequest>

#include "network/Envelope.hpp"
#include "network/MessageDispatcher.hpp"
#include "network/ThreadMarshal.hpp"
#include "network/WsServer.hpp"

// A worker QObject that we moveToThread into a background QThread. Its
// trigger() slot executes on that worker thread — used to prove marshalToMain
// hops the callable back to the main (application) thread.
class MarshalWorker : public QObject {
    Q_OBJECT
public:
    bool ran = false;      // set true once the marshaled lambda executed
    bool onMain = false;   // true if the marshaled lambda ran on the main thread
    bool offMain = false;  // set from the worker thread proving it IS off-main
    QThread* mainThread = nullptr;

public slots:
    // Runs on the WORKER thread. Calls marshalToMain so the inner lambda is
    // posted to the main thread.
    void trigger() {
        marshalToMain([this] {
            ASSERT_MAIN_THREAD();
            onMain = (QThread::currentThread() == mainThread);
            ran = true;
        });
    }
};

// ThreadMarshalTest (T12) — proves every WS-triggered handler that touches UI /
// shared state runs on the Qt main thread. QTEST_MAIN (QCoreApplication) is
// required: marshalToMain posts a queued event that only the event loop
// delivers, and the WS integration test needs QSignalSpy::wait to pump I/O.
class ThreadMarshalTest : public QObject {
    Q_OBJECT
private slots:
    void testMarshalToMainFromMainThread();
    void testMarshalToMainFromWorkerThread();
    void testWsMessageRoutesToMainThreadHandler();
    void testAssertMainThreadCondition();
};

// 1. marshalToMain called from the main thread — the lambda still runs on the
// main thread (deferred to the next event-loop iteration, like runLater).
void ThreadMarshalTest::testMarshalToMainFromMainThread()
{
    bool ran = false;
    bool onMain = false;
    QThread* mainThread = qApp->thread();

    marshalToMain([&] {
        onMain = (QThread::currentThread() == mainThread);
        ASSERT_MAIN_THREAD(); // passes silently in debug — we ARE on main
        ran = true;
    });

    QVERIFY2(QTest::qWaitFor([&] { return ran; }, 2000),
             "marshalToMain lambda did not execute within 2s");
    QVERIFY2(onMain, "marshalToMain lambda ran on a non-main thread");
}

// 2. marshalToMain called from a worker thread — the lambda runs on the MAIN
// thread, NOT the worker thread. This is the core guarantee: any callable
// posted from WS I/O context lands on the main thread before touching state.
void ThreadMarshalTest::testMarshalToMainFromWorkerThread()
{
    QThread thread;
    MarshalWorker worker;
    worker.mainThread = qApp->thread();
    worker.moveToThread(&thread);
    thread.start();

    // Post trigger() to the worker thread (queued — runs on `thread`).
    QMetaObject::invokeMethod(&worker, &MarshalWorker::trigger,
                              Qt::QueuedConnection);

    QVERIFY2(QTest::qWaitFor([&] { return worker.ran; }, 5000),
             "marshalToMain lambda from worker never executed within 5s");
    QVERIFY2(worker.onMain,
             "lambda posted from worker thread did not run on the main thread");

    thread.quit();
    QVERIFY2(thread.wait(2000), "worker thread did not stop within 2s");
}

// 3. Integration — a real inbound WS text frame flows through WsServer →
// MessageDispatcher → registered event handler, and the handler asserts it is
// on the main thread (ASSERT_MAIN_THREAD passes without abort). This proves
// the end-to-end WS→handler path lands on the main thread.
void ThreadMarshalTest::testWsMessageRoutesToMainThreadHandler()
{
    WsServer server;
    QVERIFY2(server.listen(0), "server should listen on 127.0.0.1:0");
    const quint16 port = server.serverPort();

    MessageDispatcher dispatcher;
    // Phase 5 todo 11: messageReceived now carries (int instanceId, env).
    // MessageDispatcher::dispatch takes only env — wrap in a lambda that
    // drops the instanceId (this test has one client, id is irrelevant).
    QObject::connect(&server, &WsServer::messageReceived,
                     &dispatcher, [&dispatcher](int, const Envelope& env) {
                         dispatcher.dispatch(env);
                     });

    bool handlerRan = false;
    bool handlerOnMain = false;
    QThread* mainThread = qApp->thread();

    dispatcher.registerEventHandler(QStringLiteral("ready"), [&](const Envelope&) {
        // This handler mutates UI-facing flags — it MUST be on the main thread.
        ASSERT_MAIN_THREAD();
        handlerOnMain = (QThread::currentThread() == mainThread);
        handlerRan = true;
    });

    // Gate c (T8): token must be registered before the client connects.
    server.registerToken(1, QStringLiteral("tok123"));

    QWebSocket client;
    QSignalSpy serverConnectedSpy(&server, &WsServer::connectionStateChanged);
    QSignalSpy clientConnectedSpy(&client, &QWebSocket::connected);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/?instance_id=1&token=tok123")
                         .arg(port)));

    // Wait for BOTH sides so sendTextMessage writes into a fully established
    // socket (T5 learning: sending before the handshake resolves loses bytes).
    QVERIFY2(serverConnectedSpy.wait(5000), "server Connected not received");
    if (clientConnectedSpy.isEmpty())
        QVERIFY2(clientConnectedSpy.wait(2000), "client connected not received");

    // Send a valid event text frame. "ready" is a real protocol event; WsServer
    // internally stops its ready-timer for it AND emits messageReceived so the
    // dispatcher routes it to our handler.
    const Envelope evt = createEvent(QStringLiteral("ready"));
    const QByteArray bytes = serialize(evt).toJson(QJsonDocument::Compact);
    const qint64 written = client.sendTextMessage(QString::fromUtf8(bytes));
    QVERIFY2(written > 0, "sendTextMessage wrote 0 bytes");

    QVERIFY2(QTest::qWaitFor([&] { return handlerRan; }, 5000),
             "WS event handler did not fire within 5s");
    QVERIFY2(handlerOnMain,
             "WS event handler ran on a non-main thread");

    client.close();
    server.close();
}

// 4. ASSERT_MAIN_THREAD passes on the main thread, and the condition it checks
// (currentThread == qApp->thread()) is FALSE on a worker thread — proving the
// macro WOULD fire there. We never actually trigger the abort; we only verify
// the condition evaluates to the "would fail" state off-main.
void ThreadMarshalTest::testAssertMainThreadCondition()
{
    // On the main thread: ASSERT_MAIN_THREAD passes silently (no abort).
    ASSERT_MAIN_THREAD();
    QVERIFY2(QThread::currentThread() == qApp->thread(),
             "test body should be running on the main thread");

    // On a worker thread: prove currentThread != mainThread WITHOUT calling
    // ASSERT_MAIN_THREAD (that would abort the process in a debug build).
    QThread thread;
    MarshalWorker worker;
    worker.moveToThread(&thread);
    thread.start();

    QMetaObject::invokeMethod(&worker, [&]() {
        worker.offMain = (QThread::currentThread() != qApp->thread());
    }, Qt::QueuedConnection);

    QVERIFY2(QTest::qWaitFor([&] { return worker.offMain; }, 5000),
             "worker-thread condition check did not complete within 5s");
    QVERIFY2(worker.offMain,
             "worker thread == mainThread — ASSERT_MAIN_THREAD would NOT fire "
             "(condition is not detecting off-main execution)");

    thread.quit();
    QVERIFY2(thread.wait(2000), "worker thread did not stop within 2s");
}

QTEST_MAIN(ThreadMarshalTest)
#include "ThreadMarshalTest.moc"
