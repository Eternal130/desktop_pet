package com.desktoppet.core;

import com.desktoppet.model.ControllerStats;
import oshi.SystemInfo;
import oshi.software.os.OSProcess;
import oshi.software.os.OperatingSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.lang.management.ManagementFactory;
import java.lang.management.MemoryMXBean;
import java.lang.management.MemoryUsage;
import java.util.Objects;

/**
 * Samples the controller JVM's own resource usage via OSHI (CPU + RSS) and the
 * JDK {@link MemoryMXBean} (JVM heap). Intended to be called from a dedicated
 * {@code monitor-executor} thread (Wave 2 / T4); never touch this from a
 * WebSocket callback thread.
 *
 * <h3>Design notes</h3>
 * <ul>
 *   <li><b>OSHI warmup</b>: {@link OSProcess#getProcessCpuLoadBetweenTicks(OSProcess)}
 *       needs a prior snapshot to diff against. The constructor captures that
 *       baseline so the first real {@link #collectControllerStats()} call has
 *       something to subtract. Each subsequent call rolls the snapshot forward.</li>
 *   <li><b>Heap source</b>: {@link SystemInfo#getHardware()} exposes system RAM,
 *       not JVM heap. The {@code heapUsedBytes}/{@code heapMaxBytes} fields have
 *       JVM-heap semantics, so the canonical source is
 *       {@link ManagementFactory#getMemoryMXBean()}.</li>
 *   <li><b>Failure semantics</b>: every OSHI/JDK call is wrapped. On failure the
 *       returned record carries zero-valued fields rather than throwing — the
 *       resource-monitor page renders "—".</li>
 *   <li><b>No renderer stats here</b>: renderer-process metrics arrive via the
 *       {@code stats_state} WebSocket event (Wave 2). See plan T2 / Oracle R6.</li>
 *   <li><b>Threading</b>: not thread-safe. Single-threaded by contract
 *       ({@code monitor-executor}); the {@code priorSnapshot} field is owned
 *       exclusively by that thread.</li>
 * </ul>
 */
public final class ResourceStatsCollector {

    private static final Logger log = LoggerFactory.getLogger(ResourceStatsCollector.class);

    private final SystemInfo systemInfo;
    private final MemoryMXBean heapBean;
    private final int selfPid;

    private OSProcess priorSnapshot;

    /**
     * Production-friendly constructor; captures the OSHI warmup snapshot.
     *
     * @param systemInfo OSHI entry point (injected for testability)
     */
    public ResourceStatsCollector(SystemInfo systemInfo) {
        this.systemInfo = Objects.requireNonNull(systemInfo, "systemInfo must not be null");
        this.heapBean = ManagementFactory.getMemoryMXBean();
        this.selfPid = (int) ProcessHandle.current().pid();
        this.priorSnapshot = captureSnapshot();
    }

    private OSProcess captureSnapshot() {
        try {
            return systemInfo.getOperatingSystem().getProcess(selfPid);
        } catch (RuntimeException e) {
            log.warn("OSHI snapshot failed; CPU load will stay 0 until next success: {}", e.getMessage());
            return null;
        }
    }

    /**
     * Samples the JVM process CPU/RSS and current heap usage. Never throws.
     *
     * @return a populated {@link ControllerStats}; on sampling failure the
     *         affected fields are zero-valued while {@code timestampMs} is always set
     */
    public ControllerStats collectControllerStats() {
        long timestampMs = System.currentTimeMillis();

        double cpuPercent = 0.0;
        long rssBytes = 0L;
        try {
            OSProcess current = systemInfo.getOperatingSystem().getProcess(selfPid);
            if (current != null) {
                OSProcess prior = priorSnapshot;
                if (prior != null) {
                    double load = current.getProcessCpuLoadBetweenTicks(prior);
                    if (load >= 0.0) {
                        cpuPercent = load * 100.0;
                    }
                }
                rssBytes = current.getResidentSetSize();
                priorSnapshot = current;
            }
        } catch (RuntimeException e) {
            log.warn("OSHI JVM process sampling failed: {}", e.getMessage());
        }

        long heapUsedBytes = 0L;
        long heapMaxBytes = 0L;
        try {
            MemoryUsage heap = heapBean.getHeapMemoryUsage();
            heapUsedBytes = heap.getUsed();
            long max = heap.getMax();
            heapMaxBytes = max < 0 ? 0L : max;
        } catch (RuntimeException e) {
            log.warn("JVM heap sampling failed: {}", e.getMessage());
        }

        return new ControllerStats(cpuPercent, rssBytes, heapUsedBytes, heapMaxBytes, timestampMs);
    }
}
