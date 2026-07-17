#include "core/InstanceSession.hpp"

#include "network/Protocol.hpp"

#include <spdlog/spdlog.h>
#include "logging/Logging.hpp"

// InstanceSessionMonitor — Phase-5 Wave 8 todo 18 handler bodies extracted
// from InstanceSession.cpp to keep the orchestrator's main implementation
// under the 250 pure-LOC ceiling (same split discipline as
// InstanceSessionSetters.cpp / InstanceSessionCommands.cpp /
// InstanceSessionHandlers.cpp). Two methods:
//   - onMonitorTick: 2s QTimer fires → collect self stats + send get_stats.
//   - handleStatsStateEvent: stats_state EVENT → parse + merge into model.
// Plus the Q_INVOKABLE monitorModel() accessor that exposes the per-session
// MonitorDataModel to QML (MonitorPage.qml).

QObject* InstanceSession::monitorModel()
{
    return &m_monitorModel;
}

void InstanceSession::onMonitorTick()
{
    // (1) Collect the controller's own CPU/RSS. Always succeeds (never-throws
    // per ResourceStatsCollector contract); the first sample after start has
    // cpuPercent=0 (no baseline yet). mergeController carries the prior
    // renderer sample forward so the trend chart stays continuous.
    const ControllerStats cs = m_statsCollector.collect();
    m_monitorModel.mergeController(cs);

    // (2) Poll the renderer — but ONLY when connected. stats_state arrives
    // via the EVENT channel (interface.md §K), NOT as a Response, so
    // sendCommand (which writes to the WS socket) is the correct vehicle.
    // There is NO PendingRequests entry (per the protocol's "do not register
    // a pending request for get_stats" rule — it would time out at 10s).
    if (!m_connected) {
        // Pre-connection ticks (between start() and ready) are harmless: they
        // keep populating the controller side so the monitor page shows
        // controller activity even before the renderer connects. Logged at
        // DEBUG to avoid spam during the (typically sub-second) gap.
        LOG_DEBUG("InstanceSession[{}]: monitor tick skipped get_stats "
                  "(not connected)", m_instanceId);
        return;
    }

    sendCommand(Protocol::buildGetStats());
}

void InstanceSession::handleStatsStateEvent(const Envelope& env)
{
    // interface.md §K — stats_state payload fields (see RendererStats::fromJson
    // for the per-field null tolerance). gpu_percent / gpu_name / vram_* are
    // JSON null on Linux Stub; cpu_percent / rss_bytes / timestamp_ms always.
    //
    // Parse + merge on the Qt main thread (MessageDispatcher marshals all
    // events here); the model emits snapshotAppended which MonitorPage.qml
    // catches to refresh the 6 charts + stale banner. No thread hop needed.
    const RendererStats rs = RendererStats::fromJson(env.payload);
    m_monitorModel.mergeRenderer(rs);
    LOG_DEBUG("InstanceSession[{}]: stats_state merged (cpu={:.1f}% rss={}B "
              "gpu={} vram={})",
              m_instanceId, rs.cpuPercent, rs.rssBytes,
              rs.gpuPercent.has_value() ? "set" : "null",
              rs.vramUsedBytes.has_value() ? "set" : "null");
}
