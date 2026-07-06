package com.desktoppet.model;

/**
 * Resource-usage snapshot of the controller JVM process, produced by
 * {@code ResourceStatsCollector.collectControllerStats()} and consumed by
 * the resource-monitor page (Wave 1 / T2).
 *
 * <p>All fields are point-in-time samples captured at {@code timestampMs};
 * no historical aggregation is performed at this layer.
 *
 * @param cpuPercent    JVM process CPU load since the previous sample, 0..100
 *                      (0.0 on the first sample after construction or on failure)
 * @param rssBytes      resident set size of the JVM process in bytes (0 on failure)
 * @param heapUsedBytes used JVM heap bytes from {@code MemoryMXBean} (0 on failure)
 * @param heapMaxBytes  max JVM heap bytes (-1 translated to 0; 0 on failure)
 * @param timestampMs   epoch milliseconds at which the sample was captured
 */
public record ControllerStats(
        double cpuPercent,
        long rssBytes,
        long heapUsedBytes,
        long heapMaxBytes,
        long timestampMs
) {}
