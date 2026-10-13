#pragma once

#include <chrono>

// Pure throttle for the motion_progress event stream
// (docs/protocol/events.md §17). Owns exactly one contract:
//
//   While a motion is playing, at most ONE snapshot emission per interval
//   (default 500ms); while idle the window keeps sliding so a newly started
//   motion's first snapshot lands one full interval after playback becomes
//   active (t=0 is already bracketed by motion_started).
//
// Deliberately free of Cubism/GL/network dependencies (only <chrono>) so the
// cadence contract is unit-testable with deterministic time points.
class MotionProgressThrottle {
public:
    using Clock = std::chrono::steady_clock;

    explicit MotionProgressThrottle(std::chrono::milliseconds interval =
                                        std::chrono::milliseconds(500))
        : _interval(interval) {}

    // Feed the current motion-active state; returns true when a
    // motion_progress snapshot is due NOW. On true the emission time is
    // recorded by the throttle itself — the caller must emit exactly once.
    //
    //   idle  → always false; window start tracks `now` (zero emissions,
    //           zero accumulation — the idle path is pure bookkeeping)
    //   rising edge (idle→active) → false; opens a fresh window so the first
    //           snapshot is due at +interval
    //   active → true iff a full interval elapsed since the window opened /
    //           since the last emission (window then restarts at `now`)
    bool poll(bool motionActive, Clock::time_point now) {
        if (!motionActive) {
            _windowStart = now;
            _wasActive = false;
            return false;
        }

        if (!_wasActive) {
            _windowStart = now;
            _wasActive = true;
            return false;
        }

        if (now - _windowStart >= _interval) {
            _windowStart = now;
            return true;
        }
        return false;
    }

private:
    std::chrono::milliseconds _interval;
    bool _wasActive = false;
    Clock::time_point _windowStart{};
};
