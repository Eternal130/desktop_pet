#include "core/ProcessManager.hpp"

#include <QFileInfo>
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
    // a non-zero code. Both indicate abnormal termination that the crash-
    // recovery logic (blueprint §4.6.3) should treat as a potential restart
    // trigger.
    //
    // Known exception: the renderer has a teardown access-violation
    // (0xC0000005) that fires AFTER the WS connection is cleanly closed and
    // the `shutdown` command was processed (T6 PoC finding). The instance
    // manager (T16) tolerates this by checking a `manuallyStopping` flag
    // before triggering crash recovery — ProcessManager faithfully reports
    // crashed=true and lets the caller decide.
    const bool crashed = (exitStatus == QProcess::CrashExit) || (exitCode != 0);

    LOG_INFO("ProcessManager: renderer exited exitCode={} exitStatus={} crashed={}",
             exitCode, static_cast<int>(exitStatus), crashed);

    emit exited(exitCode, crashed);
}
