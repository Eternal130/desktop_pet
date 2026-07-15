// Integration test (task T6): launches the PoC binary and asserts it completes
// the full WS↔renderer round-trip (exit 0 + stdout "PoC SUCCESS"). NOT a unit
// test of the PoC internals — it treats the PoC as a black-box subprocess.
//
// The renderer opens a live GLFW/OpenGL window, so this test requires a real
// display; it cannot run in a fully headless CI. QSKIP fires if either binary
// (PoC or renderer) is missing, and the REQUIRES_RENDERER ctest label allows
// filtering it out of headless runs.

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTest>

class PoCIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void testPoCRoundTrip()
    {
        const QString binDir = QStringLiteral(BIN_OUTPUT_DIR);
        const QString pocPath = binDir + "/desktop-pet-controller-qt-poc.exe";
        const QString rendererPath = binDir + "/desktop-pet-renderer.exe";

        if (!QFile::exists(pocPath))
            QSKIP("PoC binary absent");
        if (!QFile::exists(rendererPath))
            QSKIP("renderer binary absent");

        QProcess poc;
        poc.setWorkingDirectory(binDir);
        poc.start(pocPath, {});

        QVERIFY2(poc.waitForFinished(30000),
                 "PoC did not finish within 30s");
        QCOMPARE(poc.exitStatus(), QProcess::NormalExit);
        QCOMPARE(poc.exitCode(), 0);

        const QString output = QString::fromUtf8(poc.readAllStandardOutput());
        QVERIFY2(output.contains("PoC SUCCESS"),
                 qPrintable("PoC stdout was:\n" + output));
    }
};

QTEST_MAIN(PoCIntegrationTest)
#include "PoCIntegrationTest.moc"
