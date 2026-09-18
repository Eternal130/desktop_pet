#pragma once

#include <QObject>
#include <QJsonObject>
#include <QVariantList>
#include <QVector>
#include <QtGlobal>
#include <optional>
#include <QString>

#include "system/ResourceStatsCollector.hpp"  // ControllerStats (todo 17)

// MonitorDataModel (Wave 8 todo 18) — copy-on-write ring buffer of resource-
// monitor snapshots for the MonitorPage.
//
// Two merge paths feed the model:
//   - mergeController(const ControllerStats&) — driven by the 2s QTimer poller
//     inside InstanceSession (ResourceStatsCollector::collect → here). The
//     controller side is ALWAYS available (we collect our own process stats).
//   - mergeRenderer(const RendererStats&) — driven by the stats_state EVENT
//     handler (interface.md §K). The renderer side may have null GPU/VRAM on
//     Linux stub (interface.md §K line "vram_used_bytes ... Linux Stub ... null").
//
// Each merge appends a MonitorSnapshot that CARRIES FORWARD the prior half:
//   - mergeController preserves the previous renderer sample (if any) so a
//     partial snapshot (controller-only) does not blank the renderer columns.
//   - mergeRenderer preserves the previous controller sample (if any).
//   This mirrors Java's MonitorSnapshot merge semantics exactly.
//
// HISTORY_CAP=60 caps the trend buffer (Java reference constant). Trimming is
// FIFO (removeFirst) once size>cap — same observable behavior as the Java
// ArrayDeque-based ring.
//
// **Threading**: same contract as Java — writers (mergeController /
// mergeRenderer / clearHistory) MUST run on the Qt main thread (the poller
// QTimer fires there; the stats_state handler runs there via
// MessageDispatcher's main-thread marshal). Readers (Q_INVOKABLE getters) are
// safe from any thread but in practice always called from QML on the main
// thread. NO mutex: single-threaded by ownership of the InstanceSession that
// owns this model.
//
// **Stale detection**: isStale(nowMs, lastUpdateMs, thresholdMs) is a PURE
// static predicate so tests can exercise it without injecting a clock. The
// QML page calls isStaleNow() (which reads QDateTime::currentMSecsSinceEpoch
// + m_lastRendererUpdateMs) for the banner.

// Renderer-process resource snapshot.
// GPU/VRAM fields are std::optional<> because the Linux Stub returns null on
// the wire (interface.md §K: "vram_used_bytes ... Linux Stub ... null"). The
// nullable fields MAY be JSON `null` OR absent; fromJson accepts both.
//
// **Field names match the WIRE FORMAT verbatim** (interface.md §K +
// renderer/src/monitor/StatsPayload.cpp): `cpu_percent`, `rss_bytes`,
// `gpu_percent`, `gpu_name`, `vram_used_bytes`, `vram_total_bytes`,
// `timestamp_ms`. NOT the shortened aliases the task spec paraphrased.
struct RendererStats
{
    // Process CPU load 0..100+ (multi-threaded renderer can exceed 100).
    // Always available per interface.md §K.
    double cpuPercent = 0.0;

    // Resident set size in BYTES. Always available per interface.md §K.
    qint64 rssBytes = 0;

    // GPU utilization 0..100. std::nullopt on Linux Stub / collection failure.
    std::optional<double> gpuPercent;

    // GPU adapter name (e.g. "NVIDIA GeForce RTX 3060"). Empty / nullopt on
    // Linux Stub. Kept as a plain QString (not optional) — empty string is
    // the natural null sentinel for UI display.
    QString gpuName;

    // VRAM used (bytes). std::nullopt when DXGI unavailable (Linux Stub).
    std::optional<qint64> vramUsedBytes;

    // VRAM total (bytes). std::nullopt when DXGI unavailable (Linux Stub).
    std::optional<qint64> vramTotalBytes;

    // Renderer-side epoch-ms timestamp. Always available per interface.md §K.
    qint64 timestampMs = 0;

    // Parse a stats_state payload (env.payload). Tolerant: missing keys /
    // JSON null / wrong type → field stays at its default (0 / empty /
    // nullopt). Mirrors Java RendererStats.fromJson's hasNonNull guards.
    static RendererStats fromJson(const QJsonObject& payload);
};

// Immutable point-in-time sample. Either half is std::nullopt when only one
// side has reported (e.g. controller landed, renderer has not yet emitted
// stats_state). Mirrors Java's MonitorSnapshot record.
struct MonitorSnapshot
{
    std::optional<ControllerStats> controller;
    std::optional<RendererStats>   renderer;
    qint64 capturedAtMs = 0;
};

class MonitorDataModel : public QObject
{
    Q_OBJECT
public:
    // Maximum samples retained for the trend charts (Java HISTORY_CAP).
    static constexpr int HISTORY_CAP = 60;

    // Stale threshold: 10s without a renderer stats_state update → banner.
    // 5 missed polls at the 2s cadence (interface.md §K "陈旧判定：超过 10 秒").
    static constexpr int STALE_THRESHOLD_MS = 10000;

    explicit MonitorDataModel(QObject* parent = nullptr);

    // ── Writers (Qt main thread only) ──────────────────────────────────────

    // Append a controller sample (preserves any prior renderer sample).
    void mergeController(const ControllerStats& cs);

    // Append a renderer sample (preserves any prior controller sample) +
    // updates the last-renderer-update timestamp used for stale detection.
    void mergeRenderer(const RendererStats& rs);

    // Wipe all history + reset stale timestamp. Used on instance switch.
    void clearHistory();

    // ── Pure static stale predicate (tested directly) ──────────────────────
    // Returns true when lastUpdateMs has NEVER been set (<=0) OR the gap from
    // lastUpdate to now exceeds thresholdMs. Strict `>` so the boundary tick
    // is not flagged (matches Java isStaleAt).
    static bool isStale(qint64 nowMs, qint64 lastUpdateMs, int thresholdMs = STALE_THRESHOLD_MS);

    // ── Readers (any thread; in practice Qt main thread via QML) ───────────

    // The most recent snapshot. std::nullopt if no merge has run yet.
    std::optional<MonitorSnapshot> latestSnapshot() const;

    // Full ring buffer (max HISTORY_CAP). QVector is implicitly shared so the
    // copy is cheap until the caller mutates.
    QVector<MonitorSnapshot> history() const;

    // Epoch-ms of the most recent mergeRenderer call. 0 if none yet.
    qint64 lastRendererUpdateMs() const { return m_lastRendererUpdateMs; }

    // ── QML-friendly accessors (return scalar QVariant-friendly values) ────
    // Each returns 0 / -1 / empty for the "no data yet" case so the QML page
    // can render "—" uniformly via a sentinel check. -1 sentinel is used for
    // the nullable GPU/VRAM getters (QML: `value < 0 ? "—" : fmt(value)`).
    Q_INVOKABLE double latestControllerCpuPercent() const;
    Q_INVOKABLE double latestControllerRssBytes() const;
    Q_INVOKABLE double latestRendererCpuPercent() const;
    Q_INVOKABLE double latestRendererRssBytes() const;
    // -1.0 sentinel when null (Linux stub) — QML shows "—".
    Q_INVOKABLE double latestRendererGpuPercent() const;
    Q_INVOKABLE QString latestRendererGpuName() const;
    // -1.0 sentinel when null.
    Q_INVOKABLE double latestRendererVramUsedBytes() const;
    Q_INVOKABLE double latestRendererVramTotalBytes() const;

    // True when no renderer stats_state has arrived within STALE_THRESHOLD_MS.
    // Reads the wall clock itself — QML just binds `monitorModel.isStaleNow`.
    // Marked const + Q_INVOKABLE so QML can call it from a binding.
    Q_INVOKABLE bool isStaleNow() const;

    // Epoch-ms helpers for the QML "last update" label.
    Q_INVOKABLE qint64 lastRendererUpdateMsAsInt() const { return m_lastRendererUpdateMs; }
    Q_INVOKABLE qint64 nowMs() const;

    // ── Chart series (flat QVariantList of y-values, x = sample index) ─────
    // Each call rebuilds the list from the current history. The QML page
    // replaces the LineSeries points on snapshotAppended. GPU/VRAM series
    // skip samples where the field was null (so the line breaks cleanly).
    Q_INVOKABLE QVariantList controllerCpuSeries() const;
    Q_INVOKABLE QVariantList controllerRssSeries() const;
    Q_INVOKABLE QVariantList rendererCpuSeries() const;
    Q_INVOKABLE QVariantList rendererRssSeries() const;
    Q_INVOKABLE QVariantList rendererGpuSeries() const;   // skips null samples
    Q_INVOKABLE QVariantList rendererVramSeries() const;  // skips null samples

signals:
    // Emitted after every mergeController / mergeRenderer append. QML connects
    // this to a handler that refreshes the value labels + chart series.
    void snapshotAppended();

    // Emitted by clearHistory. QML clears the chart series + resets labels.
    void historyCleared();

private:
    void appendAndTrim(const MonitorSnapshot& snap);

    QVector<MonitorSnapshot> m_history;
    std::optional<MonitorSnapshot> m_latest;
    qint64 m_lastRendererUpdateMs = 0;
};
