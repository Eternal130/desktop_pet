#pragma once

#include <functional>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// ProcessManager (task T13) — QProcess wrapper managing the renderer process
// lifecycle. Formalizes the QProcess usage pattern proven by the T6 PoC
// (poc_main.cpp) into a reusable class consumed by the instance manager (T16)
// and the startup salvo (T16 wiring).
//
// Responsibilities (architecture-blueprint.md §4.6 + §3.4):
//   - Build the renderer CLI args (--port / --instance-id / --token / --model
//     + optional --x/--y/--width/--height).
//   - Set the working directory to the renderer exe's directory so Resources/
//     resolves correctly.
//   - Pump stdout/stderr line-by-line through spdlog (LOG_INFO / LOG_WARN).
//   - Emit exited(exitCode, crashed) on process termination. crashed=true when
//     QProcess::CrashExit or a non-zero exit code is observed.
//   - Provide kill() for forceful termination (SIGKILL on Unix,
//     TerminateProcess on Windows — the taskkill /F equivalent).
//
// Thread safety: ProcessManager lives on the Qt main thread. QProcess signals
// (readyReadStandardOutput, finished) are delivered on the thread that owns the
// QProcess — the Qt main thread for the controller. No GL calls happen here;
// the renderer is a separate OS process that does its own GL work.
//
// Windows tree-kill note (R4): the renderer is a single process — audio is
// handled in-process via miniaudio (AGENTS.md). QProcess::kill() terminates
// just the renderer process. If a future renderer spawns child processes,
// switch to a Windows Job Object so kill cascades to the whole tree.
class ProcessManager : public QObject {
    Q_OBJECT
public:
    explicit ProcessManager(QObject* parent = nullptr);
    ~ProcessManager() override;

    // Start the renderer process.
    //   exePath: absolute path to the renderer executable
    //   port: WS server port (renderer connects here)
    //   instanceId: instance ID for the WS URL
    //   token: auth token for WS handshake
    //   model: model name (e.g. "Hiyori")
    //   x, y: window position (-1 = default / omitted)
    //   width, height: window size (-1 = default / omitted)
    // Working directory is set to the exe's directory (so Resources/ resolves).
    // No-op (with WARN log) if a process is already running.
    void startRenderer(const QString& exePath, quint16 port, int instanceId,
                       const QString& token, const QString& model,
                       int x = -1, int y = -1, int width = -1, int height = -1);

    // Check if the renderer process is running.
    bool isRunning() const;

    // Get the PID (0 if not running).
    qint64 processId() const;

    // The CLI arguments passed to the last startRenderer() call. Returns an
    // empty list before startRenderer is called. Useful for the unit test that
    // verifies arg construction without launching a real renderer.
    QStringList arguments() const;

    // Force-kill the process immediately (SIGKILL on Unix / TerminateProcess
    // on Windows). No-op if not running.
    void kill();

    // Wait for the process to finish (up to timeoutMs). This is a BLOCKING
    // call — use only in shutdown sequences, never from the Qt event loop
    // thread of a running GUI (architecture-blueprint.md §4.6.4 step 3).
    bool waitForFinished(int timeoutMs = 5000);

    // Callback type: sends the shutdown command over WS. The app wires this to
    // wsServer->sendText(serialize(Protocol::buildShutdown()).toJson(Compact)).
    using ShutdownSender = std::function<void()>;

    // Set the callback that sends shutdown {} over WebSocket.
    // Must be set before calling stop().
    void setShutdownSender(ShutdownSender sender);

    // 3-stage graceful shutdown (architecture-blueprint.md §4.6.4):
    //   1. Set m_manuallyStopping = true, call m_shutdownSender() (sends shutdown {})
    //   2. Poll waitForFinished in 100ms ticks up to 5000ms total
    //   3. If still running after 5s → kill() (force terminate)
    // Returns true if the process exited cleanly (within 5s), false if force-killed.
    // Blocking — call from the main thread during app exit, never from the GUI
    // event loop of a running window.
    bool stop();

    // True while stop() is in progress (user-initiated shutdown). The exited()
    // signal handler can read this to distinguish user-stop (true) from
    // crash/external-kill (false). Phase 7's crash-recovery logic (§4.6.3)
    // checks this before triggering a restart.
    bool isManuallyStopping() const;

signals:
    // Emitted when the process exits. crashed=true if ExitStatus::CrashExit
    // or exitCode != 0.
    void exited(int exitCode, bool crashed);

    // Emitted for each line of stdout (also logged via spdlog as INFO).
    void stdoutLine(const QString& line);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess m_process;

    // Callback that sends the shutdown {} command over WS. Wired by the app
    // before stop() is called. Nullable — stop() no-ops the send if unset.
    ShutdownSender m_shutdownSender;

    // True while stop() is executing. Set BEFORE the shutdown command is sent,
    // cleared AFTER the exited() signal fires (in onFinished). This lets the
    // exited() slot distinguish user-initiated stop from crash/external-kill.
    // The renderer has a known teardown crash (0xC0000005) that fires AFTER the
    // WS connection cleanly closes during a `shutdown` — when this flag is
    // true, onFinished reports crashed=false so crash recovery does not fire.
    bool m_manuallyStopping = false;
};
