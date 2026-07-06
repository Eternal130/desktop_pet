package com.desktoppet.core;

import com.desktoppet.model.ControllerStats;
import oshi.SystemInfo;
import oshi.software.os.OSProcess;
import oshi.software.os.OperatingSystem;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

/**
 * Unit tests for {@link ResourceStatsCollector}.
 *
 * <p>The OSHI stack is fully mocked at the {@link SystemInfo} seam injected via
 * the constructor. {@code MemoryMXBean} is left real (it always exists in any
 * JVM and reports concrete heap numbers) so heap fields are asserted by
 * non-negativity rather than by pinned values.
 */
class ResourceStatsCollectorTest {

    private static final long HUNDRED_MB = 100L * 1024 * 1024;

    @Test
    void collectControllerStats_returnsPopulatedRecord_whenOsProcessReportsLoad() {
        // Given: OSHI mock reporting 50% CPU delta and 100MB RSS
        SystemInfo systemInfo = mock(SystemInfo.class);
        OperatingSystem os = mock(OperatingSystem.class);
        OSProcess proc = mock(OSProcess.class);
        when(systemInfo.getOperatingSystem()).thenReturn(os);
        when(os.getProcess(anyInt())).thenReturn(proc);
        when(proc.getProcessCpuLoadBetweenTicks(any(OSProcess.class))).thenReturn(0.5);
        when(proc.getResidentSetSize()).thenReturn(HUNDRED_MB);

        // When: constructor captures the warmup snapshot, then we sample once more
        ResourceStatsCollector collector = new ResourceStatsCollector(systemInfo);
        ControllerStats stats = collector.collectControllerStats();

        // Then
        assertNotNull(stats, "collectControllerStats must never return null");
        assertEquals(50.0, stats.cpuPercent(), 0.001, "cpuPercent = OSHI load * 100");
        assertEquals(HUNDRED_MB, stats.rssBytes(), "rssBytes must echo OSHI value");
        assertTrue(stats.heapUsedBytes() >= 0, "heapUsedBytes is non-negative");
        assertTrue(stats.heapMaxBytes() >= 0, "heapMaxBytes is non-negative");
        assertTrue(stats.timestampMs() > 0, "timestampMs always set");
    }

    @Test
    void collectControllerStats_returnsZeroCpu_whenWarmupSnapshotIsNull() {
        // Given: OS reports no such process for the warmup call (e.g. race)
        SystemInfo systemInfo = mock(SystemInfo.class);
        OperatingSystem os = mock(OperatingSystem.class);
        OSProcess proc = mock(OSProcess.class);
        when(systemInfo.getOperatingSystem()).thenReturn(os);
        when(os.getProcess(anyInt())).thenReturn(null, proc); // warmup=null, real call=proc
        when(proc.getProcessCpuLoadBetweenTicks(any(OSProcess.class))).thenReturn(0.5);
        when(proc.getResidentSetSize()).thenReturn(HUNDRED_MB);

        // When
        ResourceStatsCollector collector = new ResourceStatsCollector(systemInfo);
        ControllerStats stats = collector.collectControllerStats();

        // Then: no prior → CPU stays 0; RSS is still populated from the real snapshot
        assertNotNull(stats);
        assertEquals(0.0, stats.cpuPercent(), 0.0, "no prior baseline means CPU delta cannot be computed");
        assertEquals(HUNDRED_MB, stats.rssBytes(), "RSS does not depend on prior snapshot");
    }

    @Test
    void collectControllerStats_doesNotThrow_whenOshiRaises() {
        // Given: OSHI throws on every getProcess
        SystemInfo systemInfo = mock(SystemInfo.class);
        OperatingSystem os = mock(OperatingSystem.class);
        when(systemInfo.getOperatingSystem()).thenReturn(os);
        when(os.getProcess(anyInt())).thenThrow(new RuntimeException("OSHI exploded"));

        // When: warmup in constructor must also swallow the same failure
        ResourceStatsCollector collector = new ResourceStatsCollector(systemInfo);
        ControllerStats stats = collector.collectControllerStats();

        // Then: zero-valued OSHI fields, heap still populated by JDK
        assertNotNull(stats);
        assertEquals(0.0, stats.cpuPercent(), 0.0);
        assertEquals(0L, stats.rssBytes());
        assertTrue(stats.heapUsedBytes() >= 0);
        assertTrue(stats.heapMaxBytes() >= 0);
        assertTrue(stats.timestampMs() > 0, "timestamp is always set even on failure");
    }

    @Test
    void collectControllerStats_returnsZeroProcessFields_whenOsProcessIsNull() {
        // Given: OS reports no such process every time
        SystemInfo systemInfo = mock(SystemInfo.class);
        OperatingSystem os = mock(OperatingSystem.class);
        when(systemInfo.getOperatingSystem()).thenReturn(os);
        when(os.getProcess(anyInt())).thenReturn(null);

        // When
        ResourceStatsCollector collector = new ResourceStatsCollector(systemInfo);
        ControllerStats stats = collector.collectControllerStats();

        // Then
        assertNotNull(stats);
        assertEquals(0.0, stats.cpuPercent(), 0.0);
        assertEquals(0L, stats.rssBytes());
        assertTrue(stats.timestampMs() > 0);
    }
}
