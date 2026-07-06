#include "monitor/win/PdhGpuMonitor.hpp"

#include "LAppPal.hpp"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace Monitor {
namespace win {

namespace {

constexpr const wchar_t* kCounterPathW = L"\\GPU Engine(*)\\Utilization Percentage";

std::wstring pidPrefix(uint32_t pid) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"pid_%lu_", static_cast<unsigned long>(pid));
    return buf;
}

} // namespace

PdhGpuMonitor::PdhGpuMonitor() : m_pid(static_cast<uint32_t>(GetCurrentProcessId())) {}

PdhGpuMonitor::~PdhGpuMonitor() {
    disposeQuery();
}

void PdhGpuMonitor::disposeQuery() {
    if (m_counters) {
        for (size_t i = 0; i < m_counterCount; ++i) {
            if (m_counters[i]) PdhRemoveCounter(m_counters[i]);
        }
        std::free(m_counters);
        m_counters = nullptr;
        m_counterCount = 0;
    }
    if (m_query) {
        PdhCloseQuery(m_query);
        m_query = nullptr;
    }
}

void PdhGpuMonitor::refreshCountersForPid() {
    if (!m_query) return;

    DWORD bufChars = 0;
    PDH_STATUS st = PdhExpandWildCardPathW(nullptr, kCounterPathW, nullptr, &bufChars, 0);
    if (st != PDH_MORE_DATA || bufChars == 0) return;

    std::vector<wchar_t> expanded(bufChars);
    st = PdhExpandWildCardPathW(nullptr, kCounterPathW, expanded.data(), &bufChars, 0);
    if (st != ERROR_SUCCESS) return;

    const std::wstring want = pidPrefix(m_pid);
    std::vector<PDH_HCOUNTER> added;
    for (wchar_t* p = expanded.data(); *p; p += std::wcslen(p) + 1) {
        if (std::wcsstr(p, want.c_str()) == nullptr) continue;
        PDH_HCOUNTER c = nullptr;
        if (PdhAddEnglishCounterW(m_query, p, 0, &c) == ERROR_SUCCESS) {
            added.push_back(c);
        }
    }
    if (added.empty()) return;

    for (size_t i = 0; i < m_counterCount; ++i) {
        if (m_counters[i]) PdhRemoveCounter(m_counters[i]);
    }
    std::free(m_counters);
    m_counters = static_cast<PDH_HCOUNTER*>(
        std::malloc(added.size() * sizeof(PDH_HCOUNTER)));
    if (!m_counters) {
        m_counterCount = 0;
        return;
    }
    for (size_t i = 0; i < added.size(); ++i) m_counters[i] = added[i];
    m_counterCount = added.size();
}

bool PdhGpuMonitor::initialize() {
    if (m_initialized) return true;
    if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) {
        LAppPal::PrintLogLn("[PdhGpuMonitor] PdhOpenQueryW failed");
        return false;
    }
    refreshCountersForPid();

    // PDH rate counters require two collects before the first formatted
    // read is valid. Prime now so the first real sample() returns a value.
    PdhCollectQueryData(m_query);
    Sleep(500);
    PdhCollectQueryData(m_query);

    m_initialized = true;
    if (m_counterCount == 0) {
        LAppPal::PrintLogLn("[PdhGpuMonitor] no GPU engine counters for pid=%lu (driver may not expose PDH)",
                            static_cast<unsigned long>(m_pid));
    }
    return true;
}

GpuMetrics PdhGpuMonitor::sample() {
    GpuMetrics m;
    if (!m_initialized || !m_query) return m;

    // GPU engine instances may appear after init if the process first touched
    // the GPU later (shader compile, swapchain create). Re-discover once.
    if (m_counterCount == 0) {
        refreshCountersForPid();
        if (m_counterCount > 0) {
            PdhCollectQueryData(m_query);
            Sleep(16);
            PdhCollectQueryData(m_query);
        }
    }
    if (m_counterCount == 0) return m;

    if (PdhCollectQueryData(m_query) != ERROR_SUCCESS) return m;

    double total = 0.0;
    bool anyValid = false;
    for (size_t i = 0; i < m_counterCount; ++i) {
        if (!m_counters[i]) continue;
        DWORD status = 0;
        PDH_FMT_COUNTERVALUE value{};
        const PDH_STATUS rst = PdhGetFormattedCounterValue(
            m_counters[i], PDH_FMT_DOUBLE, &status, &value);
        if (rst == ERROR_SUCCESS && status == ERROR_SUCCESS) {
            const double v = value.doubleValue;
            if (v > 0.0) {
                total += v;
                anyValid = true;
            }
        }
    }

    if (anyValid) {
        if (total > 100.0) total = 100.0; // multi-engine sum can exceed 100
        m.gpuUtilizationPercent = static_cast<uint32_t>(total + 0.5);
    }
    return m;
}

} // namespace win
} // namespace Monitor
