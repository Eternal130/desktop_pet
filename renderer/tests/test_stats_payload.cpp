#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "monitor/StatsPayload.hpp"
#include "monitor/IGpuMonitor.hpp"
#include "monitor/ProcessStatsCollector.hpp"

#include <set>
#include <string>

using nlohmann::json;
using Monitor::GpuMetrics;
using Monitor::ProcessStats;
using Monitor::buildStatsPayload;

// The 7 wire keys defined by the protocol spec under docs/protocol/ — adding
// or removing any of these is a breaking protocol change. This test is the
// byte-for-byte gate referenced in plan T3 acceptance criteria.
static const std::set<std::string> kExpectedKeys = {
    "cpu_percent",
    "rss_bytes",
    "gpu_percent",
    "gpu_name",
    "vram_used_bytes",
    "vram_total_bytes",
    "timestamp_ms",
};

static void expectExactKeySet(const json& payload) {
    std::set<std::string> actual;
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        actual.insert(it.key());
    }
    EXPECT_EQ(actual, kExpectedKeys)
        << "payload keys diverged from RendererStats contract";
}

TEST(StatsPayloadTest, FullyPopulatedPayloadHasContractKeysAndValues) {
    ProcessStats ps;
    ps.cpuPercent = 42.5;
    ps.rssBytes   = 104857600ULL;

    GpuMetrics gm;
    gm.gpuUtilizationPercent = 73.5;
    gm.vramUsedBytes         = 536870912ULL;
    gm.vramTotalBytes        = 8589934592ULL;
    gm.gpuName               = "Test GPU";

    const json payload = buildStatsPayload(ps, &gm);

    expectExactKeySet(payload);
    EXPECT_DOUBLE_EQ(payload["cpu_percent"].get<double>(), 42.5);
    EXPECT_EQ(payload["rss_bytes"].get<uint64_t>(), 104857600ULL);
    EXPECT_DOUBLE_EQ(payload["gpu_percent"].get<double>(), 73.5);
    EXPECT_EQ(payload["gpu_name"].get<std::string>(), "Test GPU");
    EXPECT_EQ(payload["vram_used_bytes"].get<uint64_t>(), 536870912ULL);
    EXPECT_EQ(payload["vram_total_bytes"].get<uint64_t>(), 8589934592ULL);
    EXPECT_GT(payload["timestamp_ms"].get<int64_t>(), 0);
}

TEST(StatsPayloadTest, EmptyGpuMetricsRenderNullableFieldsAsJsonNull) {
    ProcessStats ps;
    ps.cpuPercent = 10.0;
    ps.rssBytes   = 1024ULL;

    GpuMetrics gm; // all optionals nullopt, gpuName empty
    gm.gpuName = "Stub";

    const json payload = buildStatsPayload(ps, &gm);

    expectExactKeySet(payload);
    EXPECT_TRUE(payload["gpu_percent"].is_null());
    EXPECT_EQ(payload["gpu_name"].get<std::string>(), "Stub");
    EXPECT_TRUE(payload["vram_used_bytes"].is_null());
    EXPECT_TRUE(payload["vram_total_bytes"].is_null());
}

TEST(StatsPayloadTest, NullGpuMonitorProducesAllNullGpuFields) {
    ProcessStats ps;
    ps.cpuPercent = 0.0;
    ps.rssBytes   = 0ULL;

    const json payload = buildStatsPayload(ps, nullptr);

    expectExactKeySet(payload);
    EXPECT_TRUE(payload["gpu_percent"].is_null());
    EXPECT_TRUE(payload["gpu_name"].is_null());
    EXPECT_TRUE(payload["vram_used_bytes"].is_null());
    EXPECT_TRUE(payload["vram_total_bytes"].is_null());
    EXPECT_DOUBLE_EQ(payload["cpu_percent"].get<double>(), 0.0);
}

TEST(StatsPayloadTest, TimestampAdvancesAcrossSamples) {
    ProcessStats ps{1.0, 1ULL};
    const json first  = buildStatsPayload(ps, nullptr);
    const json second = buildStatsPayload(ps, nullptr);
    EXPECT_GE(second["timestamp_ms"].get<int64_t>(),
              first["timestamp_ms"].get<int64_t>());
}
