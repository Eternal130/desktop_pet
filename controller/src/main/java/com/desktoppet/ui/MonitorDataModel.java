package com.desktoppet.ui;

import com.desktoppet.model.ControllerStats;
import com.desktoppet.model.RendererStats;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;

/**
 * Copy-on-write ring buffer of resource-monitor snapshots for the monitor page
 * (resource-monitor plan, T4). All mutation methods are designed to be called
 * on the JavaFX Application Thread (callers wrap the call in
 * {@code Platform.runLater}); the {@link #getLatestSnapshot()},
 * {@link #isStale()} and {@link #getHistory()} read paths are safe for
 * any thread because every mutable field is either {@code volatile} or
 * replaced by a new immutable {@link MonitorSnapshot} record.
 *
 * <h3>Threading model</h3>
 * <ul>
 *   <li><b>Writers</b> (FX thread only): {@link #mergeController(ControllerStats)},
 *       {@link #mergeRenderer(RendererStats)}, {@link #clearHistory()},
 *       {@link #markStale()}. The {@code monitor-executor} tick and the
 *       {@code stats_state} WS handler both hop onto the FX thread before
 *       calling these.</li>
 *   <li><b>Readers</b> (any thread): {@link #getLatestSnapshot()},
 *       {@link #isStale()}, {@link #getHistory()}. The LineChart refresh in
 *       T5's {@code MonitorPageController} reads from the FX thread but the
 *       {@code volatile} reads keep the contract honest for cross-thread
 *       probes (tests, diagnostics).</li>
 *   <li><b>History cap</b>: the {@link ArrayDeque} is mutated only on the FX
 *       thread, so it needs no external synchronization; the cap-60 trim runs
 *       inside the same critical section as the append.</li>
 * </ul>
 *
 * <p>The {@link MonitorSnapshot} record is immutable, so publishing a new one
 * via the {@code volatile} {@link #latestSnapshot} field is a safe
 * final-field-replaced publication (JLS §17.5).
 */
public final class MonitorDataModel {

    /** Maximum number of historical snapshots retained for the trend chart. */
    static final int HISTORY_CAP = 60;

    /**
     * Immutable point-in-time sample. Either half may be {@code null} when only
     * one side has reported so far (e.g. controller JVM stats landed but the
     * renderer has not yet emitted {@code stats_state}).
     */
    public record MonitorSnapshot(
            ControllerStats controller,
            RendererStats renderer,
            long capturedAtMs
    ) {}

    private volatile MonitorSnapshot latestSnapshot;
    private volatile boolean stale;
    private final Deque<MonitorSnapshot> history = new ArrayDeque<>();

    // ---- writers (FX thread) ----

    /** Merge a fresh controller sample; preserves any previously-known renderer stats. */
    void mergeController(ControllerStats cs) {
        MonitorSnapshot prev = latestSnapshot;
        RendererStats priorRenderer = prev != null ? prev.renderer() : null;
        MonitorSnapshot next = new MonitorSnapshot(cs, priorRenderer, System.currentTimeMillis());
        latestSnapshot = next;
        appendAndTrim(next);
    }

    /** Merge a fresh renderer sample; preserves any previously-known controller stats and clears stale. */
    void mergeRenderer(RendererStats rs) {
        MonitorSnapshot prev = latestSnapshot;
        ControllerStats priorController = prev != null ? prev.controller() : null;
        MonitorSnapshot next = new MonitorSnapshot(priorController, rs, System.currentTimeMillis());
        latestSnapshot = next;
        appendAndTrim(next);
        stale = false;
    }

    /** Clear all history and the latest snapshot; used on instance switch. */
    void clearHistory() {
        history.clear();
        latestSnapshot = null;
        stale = false;
    }

    /** Flag the current data as stale (no {@code stats_state} received within threshold). */
    void markStale() {
        stale = true;
    }

    // ---- readers (any thread) ----

    public boolean isStale() {
        return stale;
    }

    public MonitorSnapshot getLatestSnapshot() {
        return latestSnapshot;
    }

    /** Returns an immutable snapshot of the current history (max {@value #HISTORY_CAP} entries). */
    public List<MonitorSnapshot> getHistory() {
        return List.copyOf(history);
    }

    // ---- stale-threshold helper (pure, tested) ----

    /**
     * Pure stale-detection predicate used by the {@code monitor-executor} tick.
     * Returns {@code true} when either no renderer update has ever been seen
     * ({@code lastUpdateMs <= 0}) or the gap since the last update exceeds
     * {@code thresholdMs}. Strictly {@code >} so the boundary tick is not
     * flagged.
     */
    static boolean isStaleAt(long now, long lastUpdateMs, long thresholdMs) {
        return lastUpdateMs <= 0 || (now - lastUpdateMs) > thresholdMs;
    }

    private void appendAndTrim(MonitorSnapshot snap) {
        history.addLast(snap);
        while (history.size() > HISTORY_CAP) {
            history.removeFirst();
        }
    }
}
