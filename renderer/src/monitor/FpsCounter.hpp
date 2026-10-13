#pragma once

#include <cmath>

namespace Monitor {

// Exponential-moving-average frame-rate estimator backing the stats_state
// `fps` field (docs/protocol/events.md §11). Pure and clock-free: the render
// loop feeds per-frame delta seconds (LAppPal::GetDeltaTime in
// LAppDelegate::Run), tests feed deterministic series.
//
// Smoothing: time-constant EMA with effective window ≈ tau (default 1s) —
// after a step change in frame time the estimate converges ~63% within tau.
// Stalls (minimized window / long waits) are clamped to tau so a single
// hiccup cannot poison the average.
//
// Sentinel: fps() returns -1 until the first valid sample — the panel-side
// "unavailable" convention (gpu/vram express the same as JSON null).
class FpsCounter {
public:
    explicit FpsCounter(double smoothingTauSeconds = 1.0)
        : _tau(smoothingTauSeconds > 0.0 ? smoothingTauSeconds : 1.0) {}

    void onFrame(double deltaSeconds) {
        if (!(deltaSeconds > 0.0) || !std::isfinite(deltaSeconds)) {
            return; // invalid frame time (paused clock, first frame): ignore
        }
        if (deltaSeconds > _tau) {
            deltaSeconds = _tau; // clamp stalls to one smoothing window
        }

        if (_emaDeltaSeconds < 0.0) {
            _emaDeltaSeconds = deltaSeconds; // first sample seeds the EMA
            return;
        }

        const double alpha = deltaSeconds / (_tau + deltaSeconds);
        _emaDeltaSeconds += (deltaSeconds - _emaDeltaSeconds) * alpha;
    }

    // Smoothed frames-per-second, or -1 when no valid sample was fed yet.
    double fps() const {
        return (_emaDeltaSeconds > 0.0) ? (1.0 / _emaDeltaSeconds) : -1.0;
    }

private:
    double _tau;
    double _emaDeltaSeconds = -1.0;
};

} // namespace Monitor
