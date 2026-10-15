// AutoLaunchManagerTest — Wave 7 todo 14.
//
// Locks the OS auto-launch behavior across Windows / Linux / unsupported
// platforms. The Java reference's ProcessBuilderFactory + Supplier pattern is
// ported to std::function, so every test injects fakes that capture the
// (program, args) reg.exe invocation and return a canned exit code — NO real
// registry mutation, NO real subprocess, NO touching ~/.config/autostart.
//
// 11 slots:
//   Windows tier (5):
//     - testWindowsEnable:     captured command matches the exact `reg add`
//                              argv shape (QUOTED native-separator /d value);
//                              enable() returns true on exit 0.
//     - testWindowsEnablePathWithSpaces: a spacey install path is stored as
//                              a quoted value (the unquoted-Run-key fix).
//     - testWindowsIsEnabled:  reg query exit 0 + "DesktopPet" AND "REG_SZ"
//                              in output → true; exit 1 → false; missing
//                              REG_SZ type marker → false.
//     - testWindowsDisable:    captured command is `reg delete ... /f`;
//                              returns true.
//     - testRegExeFails:       reg add exit non-zero → enable() returns
//                              false, no throw.
//   Linux tier (5):
//     - testLinuxEnable:       .desktop file written under injected temp
//                              autostart dir; content has Type/Name/quoted
//                              Exec.
//     - testLinuxEnablePathWithSpaces: Exec="<path>" survives a spacey path.
//     - testLinuxDisable:      file gone after disable(); re-disable is a
//                              no-op (returns true).
//     - testLinuxIsEnabled:    file exists → true; absent → false.
//     - testLinuxXdgConfigHomeHonored: defaultAutostartDir() honors a set
//                              AND absolute $XDG_CONFIG_HOME (relative /
//                              unset fall back to ~/.config); the default
//                              supplier writes into the temp dir.
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
#include <QStandardPaths>
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

// Mirror of the production reg.exe program resolution (QStandardPaths::
// findExecutable + bare-"reg" fallback). The captured program string is
// environment-dependent (System32 reg.exe resolves on Windows, the fallback
// elsewhere), so tests compute the SAME answer instead of hardcoding "reg".
QString expectedRegProgram()
{
    const QString found =
        QStandardPaths::findExecutable(QStringLiteral("reg"));
    return found.isEmpty() ? QStringLiteral("reg") : found;
}

} // namespace

class AutoLaunchManagerTest : public QObject
{
    Q_OBJECT

private slots:
    // --- Windows ---
    void testWindowsEnable();
    void testWindowsEnablePathWithSpaces();
    void testWindowsIsEnabled();
    void testWindowsDisable();
    void testRegExeFails();

    // --- Linux ---
    void testLinuxEnable();
    void testLinuxEnablePathWithSpaces();
    void testLinuxDisable();
    void testLinuxIsEnabled();
    void testLinuxXdgConfigHomeHonored();

    // --- Unsupported ---
    void testUnsupportedPlatform();
};

// ===========================================================================
// Windows tier — all five use the fake regExecutor; ZERO real subprocess.
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

    // And: the captured command matches the exact expected shape. The /d
    // value is the app path converted to native separators and wrapped in
    // double quotes (the unquoted-Run-key fix — Explorer splits unquoted
    // data at the first space). QDir::toNativeSeparators mirrors the
    // production conversion so the assertion holds on every host OS.
    const QString expectedValue = QLatin1Char('"')
        + QDir::toNativeSeparators(kApp) + QLatin1Char('"');
    const QString cmd = reconstructCommand(cap.lastProgram, cap.lastArgs);
    const QString expected = expectedRegProgram() + QLatin1Char(' ')
        + QStringLiteral(
              "add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run "
              "/v DesktopPet /t REG_SZ /d %1 /f").arg(expectedValue);
    QCOMPARE_EQ(cmd, expected);
}

void AutoLaunchManagerTest::testWindowsEnablePathWithSpaces()
{
    // Given: an install path containing spaces (e.g. "C:\Program
    // Files\...") — the case the unquoted Run-key write broke (Explorer
    // would launch "C:\Program").
    RegCapture cap;
    cap.cannedExitCode = 0;
    const QString kApp =
        QStringLiteral("C:/Program Files/Desktop Pet/desktop-pet.exe");

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
    QVERIFY2(mgr.enable(), "enable() must return true when reg.exe exits 0");

    // Then: the /d value is the QUOTED native-separator path.
    const QString expectedValue = QLatin1Char('"')
        + QDir::toNativeSeparators(kApp) + QLatin1Char('"');
    const int dIndex = cap.lastArgs.indexOf(QStringLiteral("/d"));
    QVERIFY2(dIndex >= 0 && dIndex + 1 < cap.lastArgs.size(),
             "argv must contain a /d <value> pair");
    QCOMPARE_EQ(cap.lastArgs.at(dIndex + 1), expectedValue);
    QVERIFY2(!cap.lastArgs.contains(QDir::toNativeSeparators(kApp)),
             "the UNQUOTED path must not appear anywhere in argv");
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

    // When/Then (REG_SZ tighten): exit 0 + output naming the value but
    // WITHOUT the REG_SZ type marker (e.g. the value exists with another
    // type) → isEnabled false — the type marker is required.
    cap.cannedExitCode = 0;
    cap.cannedOutput = QStringLiteral(
        "    DesktopPet    REG_DWORD    0x1\n");
    QVERIFY2(!mgr.isEnabled(),
             "isEnabled must be false when reg query output lacks REG_SZ");
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

    // And: the captured command is `<reg> delete HKCU\... /v DesktopPet /f`.
    // (Program is the findExecutable-resolved reg.exe — see
    // expectedRegProgram.)
    const QString cmd = reconstructCommand(cap.lastProgram, cap.lastArgs);
    const QString expected = expectedRegProgram() + QLatin1Char(' ')
        + QStringLiteral(
              "delete HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run "
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
// Linux tier — five slots (three QTemporaryDir content/lifecycle + one
// spacey-path + one XDG precedence lock).
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
    // trailing newline / key-order differences. Exec is QUOTED (the
    // whitespace-splitting fix); Icon stays unquoted.
    QFile f(desktopFile);
    QVERIFY2(f.open(QIODevice::ReadOnly), "failed to open .desktop for read");
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY2(content.contains(QStringLiteral("[Desktop Entry]")),
             "content must contain [Desktop Entry] header");
    QVERIFY2(content.contains(QStringLiteral("Type=Application")),
             "content must contain Type=Application");
    QVERIFY2(content.contains(QStringLiteral("Name=Desktop Pet")),
             "content must contain Name=Desktop Pet");
    QVERIFY2(content.contains(QStringLiteral("Exec=\"") + kApp
                              + QLatin1Char('"')),
             "content must contain Exec=\"<appPath>\" (quoted)");
    QVERIFY2(content.contains(QStringLiteral("Icon=") + kApp),
             "content must contain Icon=<appPath> (unquoted)");
}

void AutoLaunchManagerTest::testLinuxEnablePathWithSpaces()
{
    // Given: an install path containing spaces — the case the unquoted
    // Exec= line broke (the desktop environment would try to launch the
    // nonexistent prefix binary up to the first space).
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "temporary directory creation failed");
    const QString autostartDir = tmp.path() + QStringLiteral("/autostart");
    const QString kApp = QStringLiteral("/opt/Desktop Pet/desktop-pet.sh");

    AutoLaunchManager mgr(
        [&]() { return kApp; },
        [&]() { return autostartDir; },
        [](const QString&, const QStringList&) {
            // Linux path never invokes regExecutor; return a default.
            return AutoLaunchManager::RegResult{};
        },
        []() { return QStringLiteral("linux"); });

    // When: enable() is called.
    QVERIFY2(mgr.enable(), "enable() must succeed for a spacey path");

    // Then: Exec is the QUOTED path, and the unquoted form is absent.
    QFile f(autostartDir + QStringLiteral("/desktop-pet.desktop"));
    QVERIFY2(f.open(QIODevice::ReadOnly), "failed to open .desktop for read");
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY2(content.contains(QStringLiteral("Exec=\"") + kApp
                              + QLatin1Char('"')),
             "Exec must quote a path containing spaces");
    QVERIFY2(!content.contains(QStringLiteral("Exec=") + kApp),
             "the UNQUOTED Exec form must not appear");
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

void AutoLaunchManagerTest::testLinuxXdgConfigHomeHonored()
{
    // Locks the XDG fix: the production autostart-dir supplier honors
    // $XDG_CONFIG_HOME when set AND absolute (XDG base-dir spec); relative
    // and unset values fall back to ~/.config. defaultAutostartDir() is
    // the public static the production constructor wires as its
    // linuxConfigDirSupplier. Env is saved/restored so this slot cannot
    // leak into the other slots (all in one process under QTEST_APPLESS).
    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "temporary directory creation failed");

    const bool hadXdg = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    const QString origXdg = qEnvironmentVariable("XDG_CONFIG_HOME");

    // When/Then: absolute $XDG_CONFIG_HOME wins.
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());
    QCOMPARE_EQ(AutoLaunchManager::defaultAutostartDir(),
                tmp.path() + QStringLiteral("/autostart"));

    // And: end-to-end with the default supplier — the .desktop lands under
    // $XDG_CONFIG_HOME/autostart, NOT ~/.config/autostart.
    {
        AutoLaunchManager mgr(
            []() { return QStringLiteral("/app"); },
            []() { return AutoLaunchManager::defaultAutostartDir(); },
            [](const QString&, const QStringList&) {
                return AutoLaunchManager::RegResult{};
            },
            []() { return QStringLiteral("linux"); });
        QVERIFY2(mgr.enable(),
                 "enable() via the default dir supplier must succeed");
        QVERIFY2(QFile::exists(tmp.path()
                    + QStringLiteral("/autostart/desktop-pet.desktop")),
                 "the .desktop must be written under $XDG_CONFIG_HOME");
        QVERIFY2(mgr.disable(), "cleanup: disable() must succeed");
    }

    // When/Then: a RELATIVE $XDG_CONFIG_HOME is ignored (spec) → fallback.
    qputenv("XDG_CONFIG_HOME", QStringLiteral("relative/config").toUtf8());
    QCOMPARE_EQ(AutoLaunchManager::defaultAutostartDir(),
                QDir::homePath() + QStringLiteral("/.config/autostart"));

    // When/Then: unset → fallback.
    qunsetenv("XDG_CONFIG_HOME");
    QCOMPARE_EQ(AutoLaunchManager::defaultAutostartDir(),
                QDir::homePath() + QStringLiteral("/.config/autostart"));

    // Env hygiene: restore the pre-test state.
    if (hadXdg)
        qputenv("XDG_CONFIG_HOME", origXdg.toUtf8());
    else
        qunsetenv("XDG_CONFIG_HOME");
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
