// AutoLaunchManagerTest — Wave 7 todo 14.
//
// Locks the OS auto-launch behavior across Windows / Linux / unsupported
// platforms. The Java reference's ProcessBuilderFactory + Supplier pattern is
// ported to std::function, so every test injects fakes that capture the
// (program, args) reg.exe invocation and return a canned exit code — NO real
// registry mutation, NO real subprocess, NO touching ~/.config/autostart.
//
// 8 slots:
//   Windows tier (4):
//     - testWindowsEnable:     captured command matches the exact `reg add`
//                              argv shape; enable() returns true on exit 0.
//     - testWindowsIsEnabled:  reg query exit 0 + "DesktopPet" in output →
//                              true; exit 1 → false.
//     - testWindowsDisable:    captured command is `reg delete ... /f`;
//                              returns true.
//     - testRegExeFails:       reg add exit non-zero → enable() returns
//                              false, no throw.
//   Linux tier (3):
//     - testLinuxEnable:       .desktop file written under injected temp
//                              autostart dir; content has Type/Name/Exec.
//     - testLinuxDisable:      file gone after disable(); re-disable is a
//                              no-op (returns true).
//     - testLinuxIsEnabled:    file exists → true; absent → false.
//   Unsupported platform (1):
//     - testUnsupportedPlatform: platformSupplier returns "macos" → enable/
//                              disable return false; isEnabled returns false.
//
// QTEST_APPLESS_MAIN — QSaveFile / QFile / QDir / QString are all
// synchronous; no event loop needed. (The default regExecutor IS real
// QProcess, but tests inject fakes that never spawn anything.)

#include "system/AutoLaunchManager.hpp"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Capture bag for the fake regExecutor. Each call records (program, args)
// and returns the configured canned RegResult.
struct RegCapture
{
    QString lastProgram;
    QStringList lastArgs;
    int cannedExitCode = 0;
    QString cannedOutput;
};

// Reconstruct the full command line the manager would have run. Mirrors the
// Java test's "captured command" assertion: program + " " + args.join(" ").
// (Reg.exe argv order is part of the contract — tests assert the exact
// shape so a future refactor cannot silently swap /v and /t.)
QString reconstructCommand(const QString& program, const QStringList& args)
{
    return program + QLatin1Char(' ') + args.join(QLatin1Char(' '));
}

} // namespace

class AutoLaunchManagerTest : public QObject
{
    Q_OBJECT

private slots:
    // --- Windows ---
    void testWindowsEnable();
    void testWindowsIsEnabled();
    void testWindowsDisable();
    void testRegExeFails();

    // --- Linux ---
    void testLinuxEnable();
    void testLinuxDisable();
    void testLinuxIsEnabled();

    // --- Unsupported ---
    void testUnsupportedPlatform();
};

// ===========================================================================
// Windows tier — all four use the fake regExecutor; ZERO real subprocess.
// ===========================================================================

void AutoLaunchManagerTest::testWindowsEnable()
{
    // Given: an AutoLaunchManager wired for Windows with a capturing fake
    // regExecutor that returns exit 0. The app-path supplier returns a
    // fixed path so the assertion can match it byte-for-byte.
    RegCapture cap;
    cap.cannedExitCode = 0;
    const QString kApp = QStringLiteral("C:/bin/desktop-pet-controller-qt.exe");

    AutoLaunchManager mgr(
        [&]() { return kApp; },
        []() { return QStringLiteral("/tmp/unused-on-windows"); },
        [&cap](const QString& program, const QStringList& args) {
            cap.lastProgram = program;
            cap.lastArgs = args;
            return AutoLaunchManager::RegResult{cap.cannedExitCode,
                                                cap.cannedOutput};
        },
        []() { return QStringLiteral("windows"); });

    // When: enable() is called.
    const bool ok = mgr.enable();

    // Then: enable() returned true (exit 0 path).
    QVERIFY2(ok, "enable() must return true when reg.exe exits 0");

    // And: the captured command matches the exact expected shape. The /d value
    // is the injected app path verbatim (reg.exe accepts '/' in argument
    // values, so no escaping is needed).
    const QString cmd = reconstructCommand(cap.lastProgram, cap.lastArgs);
    const QString expected =
        QStringLiteral(
            "reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run "
            "/v DesktopPet /t REG_SZ /d %1 /f").arg(kApp);
    QCOMPARE_EQ(cmd, expected);
}

void AutoLaunchManagerTest::testWindowsIsEnabled()
{
    // Given: reg query returns exit 0 + output containing "DesktopPet".
    RegCapture cap;
    cap.cannedExitCode = 0;
    cap.cannedOutput = QStringLiteral(
        "\nHKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\n"
        "    DesktopPet    REG_SZ    C:/bin/desktop-pet.exe\n");

    AutoLaunchManager mgr(
        []() { return QStringLiteral("unused"); },
        []() { return QStringLiteral("/tmp/unused"); },
        [&cap](const QString&, const QStringList&) {
            return AutoLaunchManager::RegResult{cap.cannedExitCode,
                                                cap.cannedOutput};
        },
        []() { return QStringLiteral("windows"); });

    // When/Then: exit 0 + output contains "DesktopPet" → isEnabled true.
    QVERIFY2(mgr.isEnabled(),
             "isEnabled must be true when reg query exits 0 + output "
             "contains 'DesktopPet'");

    // When/Then: reg query exits 1 (value absent) → isEnabled false.
    cap.cannedExitCode = 1;
    cap.cannedOutput = QStringLiteral("ERROR: The system was unable to find...");
    QVERIFY2(!mgr.isEnabled(),
             "isEnabled must be false when reg query exits non-zero");
}

void AutoLaunchManagerTest::testWindowsDisable()
{
    // Given: a Windows AutoLaunchManager with a capturing fake executor.
    RegCapture cap;
    cap.cannedExitCode = 0;

    AutoLaunchManager mgr(
        []() { return QStringLiteral("unused"); },
        []() { return QStringLiteral("/tmp/unused"); },
        [&cap](const QString& program, const QStringList& args) {
            cap.lastProgram = program;
            cap.lastArgs = args;
            return AutoLaunchManager::RegResult{cap.cannedExitCode,
                                                cap.cannedOutput};
        },
        []() { return QStringLiteral("windows"); });

    // When: disable() is called.
    const bool ok = mgr.disable();

    // Then: disable() returned true.
    QVERIFY2(ok, "disable() must return true on reg delete exit 0");

    // And: the captured command is `reg delete HKCU\... /v DesktopPet /f`.
    const QString cmd = reconstructCommand(cap.lastProgram, cap.lastArgs);
    const QString expected =
        QStringLiteral(
            "reg delete HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run "
            "/v DesktopPet /f");
    QCOMPARE_EQ(cmd, expected);
}

void AutoLaunchManagerTest::testRegExeFails()
{
    // Given: reg add fails with exit code 1 (simulating permission denied or
    // reg.exe absent). The fake captures + returns the failure.
    RegCapture cap;
    cap.cannedExitCode = 1;

    AutoLaunchManager mgr(
        []() { return QStringLiteral("C:/app.exe"); },
        []() { return QStringLiteral("/tmp/unused"); },
        [&cap](const QString&, const QStringList&) {
            return AutoLaunchManager::RegResult{cap.cannedExitCode,
                                                cap.cannedOutput};
        },
        []() { return QStringLiteral("windows"); });

    // When: enable() is called against the failing reg.exe.
    const bool ok = mgr.enable();

    // Then: enable() returns false. No throw (the manager swallows subprocess
    // failures into a WARN log + false return — matches the Java reference).
    QVERIFY2(!ok, "enable() must return false when reg.exe exits non-zero");
}

// ===========================================================================
// Linux tier — three slots using QTemporaryDir as the autostart dir.
// ===========================================================================

void AutoLaunchManagerTest::testLinuxEnable()
{
    // Given: a Linux AutoLaunchManager with the autostart dir pointed at a
    // temp directory (under QTemporaryDir, auto-cleaned).
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "temporary directory creation failed");
    const QString autostartDir = tmp.path() + QStringLiteral("/autostart");
    const QString kApp = QStringLiteral("/home/user/pet/run.sh");

    AutoLaunchManager mgr(
        [&]() { return kApp; },
        [&]() { return autostartDir; },
        [](const QString&, const QStringList&) {
            // Linux path never invokes regExecutor; return a default.
            return AutoLaunchManager::RegResult{};
        },
        []() { return QStringLiteral("linux"); });

    // When: enable() is called.
    const bool ok = mgr.enable();

    // Then: enable() returned true.
    QVERIFY2(ok, "enable() must return true on a writable autostart dir");

    // And: the .desktop file exists at <autostartDir>/desktop-pet.desktop.
    const QString desktopFile = autostartDir
        + QStringLiteral("/desktop-pet.desktop");
    QVERIFY2(QFile::exists(desktopFile),
             "the .desktop file must exist after enable()");

    // And: the content has the canonical XDG keys (Type/Name/Exec). Reading
    // raw text + substring-checking — no INI parser dependency, robust to
    // trailing newline / key-order differences.
    QFile f(desktopFile);
    QVERIFY2(f.open(QIODevice::ReadOnly), "failed to open .desktop for read");
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY2(content.contains(QStringLiteral("[Desktop Entry]")),
             "content must contain [Desktop Entry] header");
    QVERIFY2(content.contains(QStringLiteral("Type=Application")),
             "content must contain Type=Application");
    QVERIFY2(content.contains(QStringLiteral("Name=Desktop Pet")),
             "content must contain Name=Desktop Pet");
    QVERIFY2(content.contains(QStringLiteral("Exec=") + kApp),
             "content must contain Exec=<appPath>");
}

void AutoLaunchManagerTest::testLinuxDisable()
{
    // Given: a Linux AutoLaunchManager with the autostart dir pointed at a
    // temp directory, and auto-launch already enabled (file written).
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "temporary directory creation failed");
    const QString autostartDir = tmp.path() + QStringLiteral("/autostart");
    const QString desktopFile = autostartDir
        + QStringLiteral("/desktop-pet.desktop");

    AutoLaunchManager mgr(
        []() { return QStringLiteral("/app"); },
        [&]() { return autostartDir; },
        [](const QString&, const QStringList&) {
            return AutoLaunchManager::RegResult{};
        },
        []() { return QStringLiteral("linux"); });
    QVERIFY2(mgr.enable(), "precondition: first enable() must succeed");
    QVERIFY2(QFile::exists(desktopFile),
             "precondition: .desktop file must exist after enable()");

    // When: disable() is called.
    bool ok = mgr.disable();

    // Then: disable() returned true AND the file is gone.
    QVERIFY2(ok, "disable() must return true when it removes the file");
    QVERIFY2(!QFile::exists(desktopFile),
             ".desktop file must be gone after disable()");

    // And: a SECOND disable() is idempotent — file already absent, but
    // disable() still returns true (matches Java's Files.deleteIfExists).
    ok = mgr.disable();
    QVERIFY2(ok,
             "disable() must return true even when the file is already absent "
             "(idempotent)");
}

void AutoLaunchManagerTest::testLinuxIsEnabled()
{
    // Given: a Linux AutoLaunchManager with autostart dir pointed at a temp
    // directory; no .desktop file exists yet.
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "temporary directory creation failed");
    const QString autostartDir = tmp.path() + QStringLiteral("/autostart");

    AutoLaunchManager mgr(
        []() { return QStringLiteral("/app"); },
        [&]() { return autostartDir; },
        [](const QString&, const QStringList&) {
            return AutoLaunchManager::RegResult{};
        },
        []() { return QStringLiteral("linux"); });

    // Then: isEnabled() returns false when the file is absent.
    QVERIFY2(!mgr.isEnabled(),
             "isEnabled must be false when the .desktop file is absent");

    // When: enable() writes the file.
    QVERIFY2(mgr.enable(), "precondition: enable() must succeed");

    // Then: isEnabled() returns true.
    QVERIFY2(mgr.isEnabled(),
             "isEnabled must be true after enable() writes the .desktop file");
}

// ===========================================================================
// Unsupported platform — macOS / other.
// ===========================================================================

void AutoLaunchManagerTest::testUnsupportedPlatform()
{
    // Given: an AutoLaunchManager whose platformSupplier reports "macos"
    // (or any non-windows/non-linux string).
    AutoLaunchManager mgr(
        []() { return QStringLiteral("/app"); },
        []() { return QStringLiteral("/unused"); },
        [](const QString&, const QStringList&) {
            return AutoLaunchManager::RegResult{};
        },
        []() { return QStringLiteral("macos"); });

    // Then: enable() / disable() / isEnabled() all return false.
    QVERIFY2(!mgr.enable(),
             "enable() must return false on an unsupported platform");
    QVERIFY2(!mgr.disable(),
             "disable() must return false on an unsupported platform");
    QVERIFY2(!mgr.isEnabled(),
             "isEnabled() must return false on an unsupported platform");
}

QTEST_APPLESS_MAIN(AutoLaunchManagerTest)
#include "AutoLaunchManagerTest.moc"
