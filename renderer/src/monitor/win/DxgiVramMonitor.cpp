#include "monitor/win/DxgiVramMonitor.hpp"

#include "LAppPal.hpp"

#include <windows.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>

#include <string>

namespace Monitor {
namespace win {

namespace {

std::string wideToUtf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

} // namespace

DxgiVramMonitor::~DxgiVramMonitor() {
    if (m_adapter) {
        m_adapter->Release();
        m_adapter = nullptr;
    }
}

bool DxgiVramMonitor::initialize() {
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        LAppPal::PrintLogLn("[DxgiVramMonitor] CreateDXGIFactory2 failed hr=0x%08lX", hr);
        return false;
    }

    IDXGIAdapter1* adapter1 = nullptr;
    hr = factory->EnumAdapters1(0, &adapter1);
    factory->Release();
    if (FAILED(hr) || !adapter1) {
        LAppPal::PrintLogLn("[DxgiVramMonitor] EnumAdapters1(0) failed hr=0x%08lX", hr);
        return false;
    }

    DXGI_ADAPTER_DESC1 desc1{};
    if (SUCCEEDED(adapter1->GetDesc1(&desc1))) {
        m_vramTotal = desc1.DedicatedVideoMemory;
        m_gpuName   = wideToUtf8(desc1.Description);
    }

    // QI for IDXGIAdapter3 (introduced in Windows 8.1) — exposes per-process
    // QueryVideoMemoryInfo. The adapter1 we hold is the same COM object.
    hr = adapter1->QueryInterface(IID_PPV_ARGS(&m_adapter));
    adapter1->Release();
    if (FAILED(hr) || !m_adapter) {
        LAppPal::PrintLogLn("[DxgiVramMonitor] QI(IDXGIAdapter3) failed hr=0x%08lX", hr);
        m_adapter = nullptr;
        return false;
    }
    return true;
}

GpuMetrics DxgiVramMonitor::sample() {
    GpuMetrics m;
    m.gpuName         = m_gpuName;
    m.vramTotalBytes  = m_vramTotal;

    if (m_adapter) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        // Node 0 = first physical GPU; LOCAL = dedicated VRAM segment.
        if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
            m.vramUsedBytes = static_cast<uint64_t>(info.CurrentUsage);
        }
    }
    return m;
}

} // namespace win
} // namespace Monitor
