// LoggingTest — TDD for the controller_qt logging infrastructure (task T2).
//
// Three behaviors locked:
//   1. Qt -> spdlog bridge: qInfo("hello %d", 42) lands "hello 42" in the log
//      file within 200ms (flush_on(info) makes it effectively immediate).
//   2. Rotation: with a small maxBytes threshold, enough INFO lines push the
//      active file over the limit and spdlog renames it to desktop-pet-qt.log.1.
//   3. Graceful degradation: pointing logDir at a non-existent path makes
//      init() return false (file sink throws) WITHOUT crashing; the console
//      sink + handler are still installed.
//
// Global-singleton caveats handled per slot:
//   - qInstallMessageHandler is a process-global pointer. Each slot saves the
//     current handler (qInstallMessageHandler(nullptr) returns the old one) and
//     restores it in cleanup().
//   - The spdlog "desktop_pet_qt" logger is dropped in cleanup() so the next
//     slot's Logging::init() re-registers cleanly.

#include "logging/Logging.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTest>
#include <QTextStream>
#include <QtLogging> // qInfo, QtMsgType, qInstallMessageHandler

#include <spdlog/spdlog.h>

namespace {

// Poll <filePath> until it contains <needle>, up to <timeoutMs>. Returns the
// file content at the first matching read (success) or the last read (for a
// verbose failure message). Used because the OS file buffer + spdlog flush
// have a small, non-zero latency even with flush_on(info).
QString waitUntilLogContains(const QString& filePath, const QString& needle,
                             int timeoutMs = 200)
{
    QString lastContent;
    for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 10) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            lastContent = QTextStream(&f).readAll();
            if (lastContent.contains(needle))
                return lastContent;
        }
        QTest::qWait(10);
    }
    return lastContent;
}

} // namespace

class LoggingTest : public QObject
{
    Q_OBJECT

private:
    QString m_baseTemp;        // per-test-case parent dir, removed on teardown
    QtMessageHandler m_savedHandler = nullptr; // restored every cleanup()

    // Create (and return) a fresh empty subdir under the base temp dir. Each
    // test method gets its own so concurrent log files never collide.
    QString makeSubdir(const QString& name) const
    {
        const QString dir = m_baseTemp + QLatin1Char('/') + name;
        QDir().mkpath(dir);
        return dir;
    }

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testQtBridgeWritesToLogFile();
    void testRotationCreatesRotatedFile();
    void testGracefulDegradationWhenDirMissing();
};

void LoggingTest::initTestCase()
{
    m_baseTemp = QDir::tempPath() + QLatin1String("/dpet-logtest-")
                 + QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(m_baseTemp);
    QVERIFY(QDir(m_baseTemp).exists());
}

void LoggingTest::cleanupTestCase()
{
    QDir(m_baseTemp).removeRecursively();
}

void LoggingTest::init()
{
    // Save whatever handler is currently installed (Qt default before any
    // Logging::init in this process) and reset to the Qt default for the slot.
    m_savedHandler = qInstallMessageHandler(nullptr);
}

void LoggingTest::cleanup()
{
    // Restore the pre-slot handler so a failed slot never leaks our bridge into
    // the next one.
    qInstallMessageHandler(m_savedHandler);
    // Drop the named logger so the next slot's init() re-registers cleanly.
    spdlog::drop(Logging::kLoggerName);
}

void LoggingTest::testQtBridgeWritesToLogFile()
{
    const QString dir = makeSubdir(QStringLiteral("bridge"));
    QVERIFY2(Logging::init(dir),
             "init should succeed for a valid, existing logDir");

    // qInfo is the C-printf form: Qt formats "hello 42" then calls our handler.
    qInfo("hello %d", 42);

    const QString content = waitUntilLogContains(
        dir + QLatin1String("/desktop-pet-qt.log"), QStringLiteral("hello 42"));
    QVERIFY2(content.contains(QStringLiteral("hello 42")),
             qPrintable(QStringLiteral(
                 "Qt bridge did not route qInfo to the log file. Content:\n")
                 + content));
}

void LoggingTest::testRotationCreatesRotatedFile()
{
    const QString dir = makeSubdir(QStringLiteral("rotation"));
    // 1 KiB threshold so ~12 formatted lines (each ~90 bytes) force a rotation.
    QVERIFY2(Logging::init(dir, /*maxBytes=*/1024),
             "init should succeed for a valid logDir");

    for (int i = 0; i < 50; ++i)
        LOG_INFO("rotation-test-line {:04d} padding-padding-padding-padding", i);
    spdlog::get(Logging::kLoggerName)->flush();

    // spdlog's rotating sink inserts the rotation index BEFORE the extension
    // (rotating_file_sink-inl.h calc_filename): "desktop-pet-qt.log" rotates to
    // "desktop-pet-qt.1.log", NOT "desktop-pet-qt.log.1". split_by_extension
    // splits at the last '.': base="desktop-pet-qt", ext=".log" -> "base.1.ext".
    const QString rotated = dir + QLatin1String("/desktop-pet-qt.1.log");
    QVERIFY2(QFile::exists(rotated),
             qPrintable(QStringLiteral("Expected rotated file at ") + rotated));
}

void LoggingTest::testGracefulDegradationWhenDirMissing()
{
    // spdlog's file_helper::open auto-creates missing parent directories
    // (os::create_dir(os::dir_name(fname))), so a merely-nonexistent dir does
    // NOT fail the sink. Force a genuine file-sink failure by making the
    // parent component an existing regular FILE: fopen on "file/log" fails
    // with ENOTDIR after open_tries_ attempts, and rotating_file_sink_mt's
    // constructor throws spdlog_ex, which Logging::init() catches.
    const QString blocker = m_baseTemp + QLatin1String("/blocker-file.txt");
    {
        QFile f(blocker);
        QVERIFY2(f.open(QIODevice::WriteOnly),
                 "precondition: create blocker file");
        f.write("x");
    }
    QVERIFY2(QFile::exists(blocker), "precondition: blocker file must exist");

    // Passing the file path as logDir yields a log path whose parent is a file.
    const bool ok = Logging::init(blocker);
    QVERIFY2(!ok,
             "init must return false when the log file cannot be created");

    // The console sink + Qt handler are still installed; calling qInfo must not
    // crash (it routes to the console-only logger).
    qInfo("degradation-no-crash");
    if (auto lg = spdlog::get(Logging::kLoggerName))
        lg->flush();
}

QTEST_MAIN(LoggingTest)
#include "LoggingTest.moc"
