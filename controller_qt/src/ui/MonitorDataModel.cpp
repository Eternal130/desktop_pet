#include "ui/MonitorDataModel.hpp"

#include <QDateTime>
#include <QJsonValue>

// MonitorDataModel — port of Java MonitorDataModel.java (127 LOC). See the
// .hpp file-level comment for the threading model + ring-buffer semantics.
//
// Implementation notes:
//   - QDateTime::currentMSecsSinceEpoch() does NOT require a QCoreApplication
//     (it's a thin wrapper over the OS epoch clock). QTEST_APPLESS_MAIN tests
//     can construct MonitorDataModel + exercise merge* without an app.
//   - QVector's implicit sharing means history() returns are cheap (copy-on-
//     write); the chart-series getters iterate the shared view without
//     detaching because they only read.
//   - The Q_INVOKABLE getters use double for ALL numeric returns because
//     QML's number type is JS double — qint64 bytes values (>2^53) would
//     truncate, but practical RSS/VRAM values are well under 2^53 bytes
//     (~9 PB). formatBytes() in QML scales them back down to GB/MB before
//     display anyway.

// ── RendererStats ────────────────────────────────────────────────────────────

RendererStats RendererStats::fromJson(const QJsonObject& p)
{
    // Mirrors Java RendererStats.fromJson + hasNonNull. Each field is read
    // defensively: missing key OR JSON null OR wrong type → field stays at
    // its default. The nullable GPU/VRAM fields explicitly check isNull()
    // before assigning so JSON null leaves the std::optional empty (NOT
    // engaged-with-zero, which would render "0 B" instead of "—").
    RendererStats r;

    const QJsonValue cpu = p.value(QStringLiteral("cpu_percent"));
    if (cpu.isDouble())
        r.cpuPercent = cpu.toDouble();

    const QJsonValue rss = p.value(QStringLiteral("rss_bytes"));
    if (rss.isDouble())
        r.rssBytes = rss.toInteger();

    // GPU percent — optional. JSON null / missing → std::nullopt (UI shows "—").
    if (p.contains(QStringLiteral("gpu_percent"))) {
        const QJsonValue v = p.value(QStringLiteral("gpu_percent"));
        if (!v.isNull() && v.isDouble())
            r.gpuPercent = v.toDouble();
    }

    // GPU name — optional but stored as QString (empty == null).
    if (p.contains(QStringLiteral("gpu_name"))) {
        const QJsonValue v = p.value(QStringLiteral("gpu_name"));
        if (!v.isNull() && v.isString())
            r.gpuName = v.toString();
    }

    // VRAM used — optional bytes.
    if (p.contains(QStringLiteral("vram_used_bytes"))) {
        const QJsonValue v = p.value(QStringLiteral("vram_used_bytes"));
        if (!v.isNull() && v.isDouble())
            r.vramUsedBytes = v.toInteger();
    }

    // VRAM total — optional bytes.
    if (p.contains(QStringLiteral("vram_total_bytes"))) {
        const QJsonValue v = p.value(QStringLiteral("vram_total_bytes"));
        if (!v.isNull() && v.isDouble())
            r.vramTotalBytes = v.toInteger();
    }

    const QJsonValue ts = p.value(QStringLiteral("timestamp_ms"));
    if (ts.isDouble())
        r.timestampMs = ts.toInteger();

    return r;
}

// ── MonitorDataModel ─────────────────────────────────────────────────────────

MonitorDataModel::MonitorDataModel(QObject* parent)
    : QObject(parent) {}

void MonitorDataModel::mergeController(const ControllerStats& cs)
{
    // Carry the prior renderer sample forward so a controller-only tick does
    // not blank the renderer columns on the next chart refresh. Matches
    // Java's MonitorSnapshot(prev.renderer) carry semantics.
    MonitorSnapshot next;
    next.capturedAtMs = QDateTime::currentMSecsSinceEpoch();
    next.controller   = cs;
    if (m_latest && m_latest->renderer)
        next.renderer = m_latest->renderer;
    m_latest = next;
    appendAndTrim(next);
    emit snapshotAppended();
}

void MonitorDataModel::mergeRenderer(const RendererStats& rs)
{
    // Carry the prior controller sample forward (symmetric to mergeController).
    // Reset staleness: the renderer just reported, so the stale banner hides.
    MonitorSnapshot next;
    next.capturedAtMs = QDateTime::currentMSecsSinceEpoch();
    next.renderer     = rs;
    if (m_latest && m_latest->controller)
        next.controller = m_latest->controller;
    m_latest = next;
    m_lastRendererUpdateMs = next.capturedAtMs;
    appendAndTrim(next);
    emit snapshotAppended();
}

void MonitorDataModel::clearHistory()
{
    m_history.clear();
    m_latest.reset();
    m_lastRendererUpdateMs = 0;
    emit historyCleared();
}

bool MonitorDataModel::isStale(qint64 nowMs, qint64 lastUpdateMs, int thresholdMs)
{
    // Java isStaleAt: never-updated (<=0) OR gap strictly greater than threshold.
    // Strict `>` so a tick landing exactly on the threshold is still "fresh".
    return lastUpdateMs <= 0 || (nowMs - lastUpdateMs) > thresholdMs;
}

std::optional<MonitorSnapshot> MonitorDataModel::latestSnapshot() const
{
    return m_latest;
}

QVector<MonitorSnapshot> MonitorDataModel::history() const
{
    return m_history;
}

// ── QML-friendly scalar getters ──────────────────────────────────────────────

double MonitorDataModel::latestControllerCpuPercent() const
{
    return m_latest && m_latest->controller ? m_latest->controller->cpuPercent : 0.0;
}

double MonitorDataModel::latestControllerRssBytes() const
{
    return m_latest && m_latest->controller ? static_cast<double>(m_latest->controller->rssBytes) : 0.0;
}

double MonitorDataModel::latestRendererCpuPercent() const
{
    return m_latest && m_latest->renderer ? m_latest->renderer->cpuPercent : 0.0;
}

double MonitorDataModel::latestRendererRssBytes() const
{
    return m_latest && m_latest->renderer ? static_cast<double>(m_latest->renderer->rssBytes) : 0.0;
}

double MonitorDataModel::latestRendererGpuPercent() const
{
    // -1.0 sentinel when null (Linux stub) OR no renderer data yet — QML
    // renders "—" for negative values.
    if (!m_latest || !m_latest->renderer || !m_latest->renderer->gpuPercent)
        return -1.0;
    return *m_latest->renderer->gpuPercent;
}

QString MonitorDataModel::latestRendererGpuName() const
{
    return m_latest && m_latest->renderer ? m_latest->renderer->gpuName : QString();
}

double MonitorDataModel::latestRendererVramUsedBytes() const
{
    if (!m_latest || !m_latest->renderer || !m_latest->renderer->vramUsedBytes)
        return -1.0;
    return static_cast<double>(*m_latest->renderer->vramUsedBytes);
}

double MonitorDataModel::latestRendererVramTotalBytes() const
{
    if (!m_latest || !m_latest->renderer || !m_latest->renderer->vramTotalBytes)
        return -1.0;
    return static_cast<double>(*m_latest->renderer->vramTotalBytes);
}

bool MonitorDataModel::isStaleNow() const
{
    return isStale(QDateTime::currentMSecsSinceEpoch(), m_lastRendererUpdateMs);
}

qint64 MonitorDataModel::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

// ── Chart series ────────────────────────────────────────────────────────────
// Each getter walks m_history in order and emits the y-value for each
// sample that has the relevant field. GPU/VRAM getters SKIP samples whose
// field was null — the LineSeries will show a clean break (chart drawn from
// the post-break samples only). The x-axis is implicit (sample index 0..N-1);
// MonitorPage.qml attaches these as the y-coordinates of point list.

QVariantList MonitorDataModel::controllerCpuSeries() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.controller) out.append(s.controller->cpuPercent);
    return out;
}

QVariantList MonitorDataModel::controllerRssSeries() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.controller) out.append(static_cast<double>(s.controller->rssBytes));
    return out;
}

QVariantList MonitorDataModel::rendererCpuSeries() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.renderer) out.append(s.renderer->cpuPercent);
    return out;
}

QVariantList MonitorDataModel::rendererRssSeries() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.renderer) out.append(static_cast<double>(s.renderer->rssBytes));
    return out;
}

QVariantList MonitorDataModel::rendererGpuSeries() const
{
    // Skip null samples — Linux stub emits null gpu_percent on every tick,
    // so this returns empty on those builds (the chart shows "—" only).
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.renderer && s.renderer->gpuPercent)
            out.append(*s.renderer->gpuPercent);
    return out;
}

QVariantList MonitorDataModel::rendererVramSeries() const
{
    QVariantList out;
    out.reserve(m_history.size());
    for (const auto& s : m_history)
        if (s.renderer && s.renderer->vramUsedBytes)
            out.append(static_cast<double>(*s.renderer->vramUsedBytes));
    return out;
}

// ── Private ──────────────────────────────────────────────────────────────────

void MonitorDataModel::appendAndTrim(const MonitorSnapshot& snap)
{
    m_history.append(snap);
    while (m_history.size() > HISTORY_CAP)
        m_history.removeFirst();
}
