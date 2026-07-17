#include "core/ProcessManager.hpp"

#include <utility>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QString>
#include <QStringList>

// spdlog MUST be included before logging/Logging.hpp — Logging.hpp references
// the SPDLOG_* macros but does NOT include spdlog headers itself (T5 finding
// in .omo/notepads/qt-controller-foundation/learnings.md).
#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

ProcessManager::ProcessManager(QObject* parent)
    : QObject(parent)
{
    // Connect signals once in the constructor (not in startRenderer) so
    // repeated startRenderer calls don't double-connect. The QProcess lives
    // for the lifetime of this ProcessManager.
    connect(&m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProcessManager::onFinished);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &ProcessManager::onReadyReadStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &ProcessManager::onReadyReadStandardError);
    // T25: FailedToStart (exe exists but OS can't launch — permissions,
    // corrupt binary, wrong arch). Qt 6 does NOT emit finished() for this,
    // so without this connection InstanceSession would stay "connecting"
    // forever. onErrorOccurred emits exited(-1, true) → crash path.
    connect(&m_process, &QProcess::errorOccurred,
            this, &ProcessManager::onErrorOccurred);
}

ProcessManager::~ProcessManager()
{
    // If the renderer is still running when ProcessManager is destroyed,
    // kill it so we don't leak an orphan process. waitForFinished prevents
    // the QProcess destructor from detaching a half-dead child.
    if (m_process.state() != QProcess::NotRunning) {
        LOG_WARN("ProcessManager destroyed while renderer still running; killing");
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

void ProcessManager::startRenderer(const QString& exePath, quint16 port,
                                   int instanceId, const QString& token,
                                   const QString& model,
                                   int x, int y, int width, int height)
{
    if (m_process.state() != QProcess::NotRunning) {
        LOG_WARN("ProcessManager::startRenderer called while a process is "
                 "already running (pid={}); ignoring", m_process.processId());
        return;
    }

    // Build CLI args (architecture-blueprint.md §3.4). Required: port,
    // instance-id, token, model. Optional: x/y/width/height (only when >= 0).
    QStringList args;
    args << QStringLiteral("--port") << QString::number(port)
         << QStringLiteral("--instance-id") << QString::number(instanceId)
         << QStringLiteral("--token") << token
         << QStringLiteral("--model") << model;
    if (x >= 0)
        args << QStringLiteral("--x") << QString::number(x);
    if (y >= 0)
        args << QStringLiteral("--y") << QString::number(y);
    if (width >= 0)
        args << QStringLiteral("--width") << QString::number(width);
    if (height >= 0)
        args << QStringLiteral("--height") << QString::number(height);

    // Working dir MUST be the renderer exe's dir so Resources/Models/<Model>/
    // resolves (blueprint §3.4, AGENTS.md, T6 PoC finding).
    const QString workDir = QFileInfo(exePath).absolutePath();
    m_process.setWorkingDirectory(workDir);

    LOG_INFO("ProcessManager: starting renderer exe=\"{}\" workDir=\"{}\" args=\"{}\"",
             exePath.toStdString(), workDir.toStdString(),
             args.join(' ').toStdString());

    m_process.start(exePath, args);
}

bool ProcessManager::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

qint64 ProcessManager::processId() const
{
    return m_process.processId();
}

QStringList ProcessManager::arguments() const
{
    return m_process.arguments();
}

void ProcessManager::kill()
{
    if (m_process.state() == QProcess::NotRunning)
        return;
    LOG_WARN("ProcessManager: force-killing renderer (pid={})",
             m_process.processId());
    // QProcess::kill() sends SIGKILL on Unix, TerminateProcess on Windows.
    // The finished() signal (→ onFinished → exited) fires asynchronously.
    m_process.kill();
}

bool ProcessManager::waitForFinished(int timeoutMs)
{
    return m_process.waitForFinished(timeoutMs);
}

void ProcessManager::setShutdownSender(ShutdownSender sender)
{
    m_shutdownSender = std::move(sender);
}

bool ProcessManager::isManuallyStopping() const
{
    return m_manuallyStopping;
}

bool ProcessManager::stop()
{
    // Already stopped — nothing to do. Return true (clean state).
    if (m_process.state() == QProcess::NotRunning)
        return true;

    // Set the flag BEFORE sending shutdown so onFinished (which fires during
    // waitForFinished below) observes manuallyStopping=true and reports
    // crashed=false for the known teardown access-violation.
    m_manuallyStopping = true;

    // Stage 1: send `shutdown {}` over WebSocket (architecture-blueprint.md
    // §4.6.4 step 1). The sender is wired by the app via setShutdownSender().
    if (m_shutdownSender) {
        LOG_INFO("ProcessManager::stop — sending shutdown command (pid={})",
                 m_process.processId());
        m_shutdownSender();
    } else {
        LOG_WARN("ProcessManager::stop — no shutdown sender wired; "
                 "polling for exit anyway (pid={})", m_process.processId());
    }

    // Stage 2: spin a local event loop for up to 5s (100ms poll ticks,
    // blueprint §4.6.4 step 2). A QEventLoop is REQUIRED here — not bare
    // QProcess::waitForFinished — because the WebSocket send from stage 1
    // only flushes to the TCP socket when the Qt event loop runs.
    // QProcess::waitForFinished blocks the Qt event loop (it uses a native
    // WaitForSingleObject on Windows), so the shutdown JSON would sit in the
    // QWebSocket's internal buffer forever and the renderer would never exit.
    // The QEventLoop processes all events (socket I/O, QProcess signals),
    // so the shutdown reaches the renderer AND onFinished fires inside it.
    QElapsedTimer elapsed;
    elapsed.start();
    QEventLoop loop;
    QTimer pollTimer;
    pollTimer.setSingleShot(false);
    QObject::connect(&pollTimer, &QTimer::timeout, &loop, [this, &elapsed, &loop]() {
        if (m_process.state() == QProcess::NotRunning || elapsed.hasExpired(5000))
            loop.quit();
    });
    pollTimer.start(100);
    loop.exec();
    pollTimer.stop();

    if (m_process.state() == QProcess::NotRunning)
        return true;

    // Stage 3: timeout — force-kill (blueprint §4.6.4 step 3).
    // SIGKILL on Unix / TerminateProcess on Windows. onFinished will fire
    // during the kill's waitForFinished with m_manuallyStopping still true,
    // so crashed=false (the force-kill is part of the user-initiated stop).
    LOG_WARN("ProcessManager::stop — renderer did not exit within 5s; "
             "force-killing (pid={})", m_process.processId());
    m_process.kill();
    m_process.waitForFinished(2000);
    return false;
}

void ProcessManager::onReadyReadStandardOutput()
{
    // Pump all available complete lines. canReadLine() guards against
    // half-finished lines (the remainder arrives on the next readyRead).
    while (m_process.canReadLine()) {
        const QByteArray raw = m_process.readLine();
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty())
            continue;
        LOG_INFO("[renderer] {}", line.toStdString());
        emit stdoutLine(line);
    }
}

void ProcessManager::onReadyReadStandardError()
{
    // stderr lines are logged as WARN but not emitted as a signal (the
    // controller only pipes stdout to the UI log buffer; stderr is rare
    // and indicates a renderer-side warning/error).
    while (m_process.canReadLine()) {
        const QByteArray raw = m_process.readLine();
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty())
            continue;
        LOG_WARN("[renderer:stderr] {}", line.toStdString());
    }
}

void ProcessManager::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // crashed = the OS reports a crash (CrashExit) OR the process exited with
    // a non-zero code — UNLESS stop() initiated this shutdown
    // (m_manuallyStopping). The renderer has a known teardown access-violation
    // (0xC0000005) that fires AFTER the WS connection is cleanly closed and
    // the `shutdown` command was processed (T6 PoC finding). When
    // m_manuallyStopping is true, the exit was user-initiated (even if the
    // renderer crashed during teardown), so crashed=false — Phase 7's crash
    // recovery (blueprint §4.6.3) must not trigger a restart.
    const bool crashed = !m_manuallyStopping
                         && (exitStatus == QProcess::CrashExit || exitCode != 0);

    LOG_INFO("ProcessManager: renderer exited exitCode={} exitStatus={} crashed={} manuallyStopping={}",
             exitCode, static_cast<int>(exitStatus), crashed, m_manuallyStopping);

    emit exited(exitCode, crashed);

    // Clear the flag AFTER exited() has been delivered to all connected slots
    // (QSignalSpy, lambdas) so they observe manuallyStopping=true when the
    // callback fires. Cleared here rather than at end of stop() so the flag
    // is reset regardless of which code path caused the process to exit.
    m_manuallyStopping = false;
}

void ProcessManager::onErrorOccurred(QProcess::ProcessError error)
{
    // FailedToStart is the only error that finished() does NOT cover. Qt 6
    // guarantees finished() fires for every other error (Crashed, Timedout,
    // etc.), so emitting exited here for those would double-fire with
    // onFinished. Only FailedToStart → emit exited so InstanceSession's
    // crash-recovery path engages instead of hanging in "connecting".
    if (error != QProcess::FailedToStart)
        return;

    LOG_ERROR("ProcessManager: renderer failed to start (FailedToStart) — "
              "emit exited as crash so InstanceSession engages recovery");
    emit exited(-1, true);
}
