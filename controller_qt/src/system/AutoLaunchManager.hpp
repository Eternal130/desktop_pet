#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

// AutoLaunchManager (Wave 7 todo 14) — OS auto-launch (startup) manager.
// Ported from Java `controller/src/.../util/AutoLaunchManager.java` (206 LOC).
//
// Platform split (mirrors the Java reference):
//   Windows: `reg.exe` add/delete/query on
//            `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value
//            'DesktopPet' via QProcess. HKCU is per-user — no UAC prompt.
//   Linux:   atomic write / delete `~/.config/autostart/desktop-pet.desktop`
//            (XDG autostart). QSaveFile mirrors the InstanceConfigManager
//            atomic-write pattern (Phase 0-4).
//   macOS/other: enable()/disable() log WARN "not supported" + return false;
//            isEnabled() returns false.
//
// Testability via injection (the Java reference's ProcessBuilderFactory +
// Supplier pattern ported to C++ std::function):
//   - appPathSupplier:        executable path written into the Run value /
//                             .desktop Exec= line. Default:
//                             QCoreApplication::applicationFilePath().
//   - linuxConfigDirSupplier: the autostart directory (parent of the .desktop
//                             file). Default: ~/.config/autostart.
//   - regExecutor:            runs `reg.exe` synchronously, returns exit code
//                             + combined stdout/stderr (so isEnabled can
//                             verify 'DesktopPet' appears in `reg query`
//                             output). Default: real QProcess. Tests inject a
//                             fake that captures (program, args) and returns a
//                             canned RegResult.
//   - platformSupplier:       returns "windows"/"linux"/"other". Default: the
//                             compile-time platform (Q_OS_WIN / Q_OS_LINUX).
//
// Production use is zero-config: `new AutoLaunchManager(this)` constructs with
// the real suppliers. Exposed as a QML context property 'autoLaunch' by
// todo 15 (closeAction) — SettingsPage binds a Checkbox to isEnabled() and
// calls enable()/disable().
class AutoLaunchManager : public QObject
{
    Q_OBJECT

public:
    // Result of running reg.exe (or any synchronous subprocess): exit code +
    // combined stdout+stderr text. The output is captured so isEnabled() can
    // verify the queried value name appears in `reg query` output. (The Java
    // reference merges stderr into stdout via redirectErrorStream.)
    struct RegResult
    {
        int exitCode = -1;
        QString output;
    };

    // Synchronous reg.exe executor seam. Default = real QProcess
    // (start + waitForFinished + readAll). Test fake = capture
    // (program, args) and return a canned RegResult — never touches the
    // real registry.
    using RegExecutor = std::function<RegResult(const QString& program,
                                                const QStringList& args)>;

    // Production constructor — wires the real platform suppliers. Zero-config.
    explicit AutoLaunchManager(QObject* parent = nullptr);

    // Test-injectable constructor. Every supplier has a documented default in
    // the production ctor above; tests pass fakes to capture commands and
    // isolate the filesystem / registry. See AutoLaunchManagerTest.
    AutoLaunchManager(std::function<QString()> appPathSupplier,
                      std::function<QString()> linuxConfigDirSupplier,
                      RegExecutor regExecutor,
                      std::function<QString()> platformSupplier,
                      QObject* parent = nullptr);

    // True if auto-launch is currently enabled on this platform.
    //   Windows: `reg query` exits 0 AND output contains "DesktopPet".
    //   Linux:   the .desktop file exists.
    //   Other:   always false.
    // Never throws — all subprocess / filesystem failures return false + WARN.
    Q_INVOKABLE bool isEnabled() const;

    // Enable auto-launch. Returns true on success, false on failure or
    // unsupported platform (logs WARN "not supported on platform '<p>'").
    // Never throws.
    Q_INVOKABLE bool enable();

    // Disable auto-launch. Idempotent — returns true when the value/file is
    // already absent (Linux QFile::remove is gated on QFile::exists first;
    // Windows reg delete exit 0 = success). Returns false only on subprocess
    // failure, filesystem error, or unsupported platform.
    Q_INVOKABLE bool disable();

private:
    // --- Windows helpers ---
    bool checkWindowsRegistry() const;
    bool enableWindows();
    bool disableWindows();

    // --- Linux helpers ---
    bool checkLinuxDesktopFile() const;
    bool enableLinux();
    bool disableLinux();
    QString desktopFilePath() const;

    // Injected suppliers (Java reference's Supplier<T> + ProcessBuilderFactory
    // ported to std::function). Stored by value — small (3 pointers + capture).
    std::function<QString()> m_appPathSupplier;
    std::function<QString()> m_linuxConfigDirSupplier;
    RegExecutor m_regExecutor;
    std::function<QString()> m_platformSupplier;
};
