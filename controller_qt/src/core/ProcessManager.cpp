#include "core/ProcessManager.hpp"

#include <utility>
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

    // S4: async-stop poller — 100ms coarse ticks while a graceful stop is
    // winding down. Parented to `this` so destruction cancels any pending
    // tick (no use-after-free through the slot).
    m_stopPollTimer.setInterval(kStopPollIntervalMs);
    m_stopPollTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_stopPollTimer, &QTimer::timeout,
            this, &ProcessManager::onStopPollTick);
}

ProcessManager::~ProcessManager()
{
    // S4: stop() is event-driven now — this sync reap is the LAST RESORT for
    // a manager destroyed with a live renderer (app teardown, scope exit
    // racing a stop). The normal path (stopFinished) should have completed
    // long before destruction. 500ms bounds the worst-case block;
    // waitForFinished also prevents the QProcess destructor from detaching a
    // half-dead child.
    if (m_process.state() != QProcess::NotRunning) {
        LOG_WARN("ProcessManager destroyed while renderer still running; killing");
        m_process.kill();
        m_process.waitForFinished(500);
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

void ProcessManager::stop()
{
    // Re-entry guard (S4): a second stop() while one is winding down must
    // not fork a second state machine / second completion emission — the
    // in-flight cycle's stopFinished serves both callers.
    if (m_stopInFlight)
        return;

    // Already stopped — nothing to do. Complete immediately (clean). Posted
    // queued so the "exited-before-stopFinished" ordering guarantee holds
    // uniformly even if a just-dead process's finished() is still in flight.
    if (m_process.state() == QProcess::NotRunning) {
        QMetaObject::invokeMethod(this, [this]() {
            emit stopFinished(true);
        }, Qt::QueuedConnection);
        return;
    }

    m_stopInFlight = true;
    m_killIssued = false;

    // Set the flag BEFORE sending shutdown so onFinished (which fires when
    // the async shutdown lands) observes manuallyStopping=true and reports
    // crashed=false for the known teardown access-violation.
    m_manuallyStopping = true;

    // Stage 1: send `shutdown {}` over WebSocket (architecture-blueprint.md
    // §4.6.4 step 1). The sender is wired by the app via setShutdownSender().
    // The WS flush happens on the normal event loop — no nested loop needed.
    if (m_shutdownSender) {
        LOG_INFO("ProcessManager::stop — sending shutdown command (pid={})",
                 m_process.processId());
        m_shutdownSender();
    } else {
        LOG_WARN("ProcessManager::stop — no shutdown sender wired; "
                 "polling for exit anyway (pid={})", m_process.processId());
    }

    // Stage 2 (async): poll the process state every 100ms until it exits or
    // the grace window elapses; stage 3 (force-kill) fires from the tick.
    m_stopElapsed.start();
    m_stopPollTimer.start();
}

bool ProcessManager::isStopping() const
{
    return m_stopInFlight;
}

void ProcessManager::setStopTimeoutMs(int ms)
{
    // Clamp to at least one poll interval so an injected window can never
    // degenerate into a busy kill.
    m_stopTimeoutMs = ms < kStopPollIntervalMs ? kStopPollIntervalMs : ms;
}

void ProcessManager::onStopPollTick()
{
    if (!m_stopInFlight)
        return; // stale tick racing finishStop's timer stop — ignore

    if (m_process.state() == QProcess::NotRunning) {
        // The exit landed without a poll-side kill (onFinished usually
        // completes the cycle first; this branch also covers exits that
        // deliver no finished(), e.g. FailedToStart during a stop).
        finishStop(!m_killIssued);
        return;
    }

    const qint64 elapsedMs = m_stopElapsed.elapsed();
    if (!m_killIssued && elapsedMs >= m_stopTimeoutMs) {
        // Stage 3: grace window exhausted — force-kill (blueprint §4.6.4
        // step 3). SIGKILL on Unix / TerminateProcess on Windows. Keep
        // polling so the death is still observed and reported.
        LOG_WARN("ProcessManager::stop — renderer did not exit within {}ms; "
                 "force-killing (pid={})", m_stopTimeoutMs,
                 m_process.processId());
        m_killIssued = true;
        m_process.kill();
        return;
    }
    if (m_killIssued && elapsedMs >= m_stopTimeoutMs + kPostKillGraceMs) {
        // Un-reapable even after SIGKILL (D-state process, zombie held by
        // another reaper) — abandon the wait; the destructor's last-resort
        // reap is all that remains. Report not-clean.
        LOG_ERROR("ProcessManager::stop — renderer un-reapable {}ms after "
                  "kill; abandoning wait (pid={})",
                  kPostKillGraceMs, m_process.processId());
        finishStop(false);
    }
}

void ProcessManager::finishStop(bool clean)
{
    if (!m_stopInFlight)
        return; // already completed (onFinished and the poll race benignly)

    m_stopPollTimer.stop();
    m_stopInFlight = false;
    m_killIssued = false;

    // QUEUED emission: the completion was triggered either by onFinished
    // (exited already emitted) or by the poll observing NotRunning (any
    // pending finished() delivery was posted before this lambda and the
    // event queue is FIFO, so exited() still fires first). Either way,
    // consumers of stopFinished — e.g. a queued restart — never observe the
    // old process's exit AFTER they have already relaunched.
    QMetaObject::invokeMethod(this, [this, clean]() {
        emit stopFinished(clean);
    }, Qt::QueuedConnection);
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

    // S4: an in-flight async stop completes right here — first-come
    // completion. Doing it inside onFinished (before the queued
    // stopFinished lands) guarantees exited() consumers always observe the
    // exit before anyone reacts to "stop complete". The poll tick's
    // NotRunning branch is the fallback for exits that deliver no
    // finished() (e.g. FailedToStart during a stop).
    if (m_stopInFlight)
        finishStop(!m_killIssued);
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
