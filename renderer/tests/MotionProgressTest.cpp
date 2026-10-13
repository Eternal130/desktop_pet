// Pure-logic tests for the two cadence/estimation helpers behind the
// motion_progress event (events.md §17) and the stats_state fps field (§11).
// Both classes are clock-free by design: tests inject deterministic time
// points / frame deltas, so every assertion below is exact.
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

#include "MotionProgressThrottle.hpp"
#include "monitor/FpsCounter.hpp"

namespace {

using Clock = MotionProgressThrottle::Clock;
using Ms = std::chrono::milliseconds;

Clock::time_point at(int ms) {
    return Clock::time_point{} + Ms(ms);
}

} // namespace

// --- MotionProgressThrottle: the "at most one snapshot per 500ms" contract --

TEST(MotionProgressThrottleTest, IdleNeverEmits)
{
    MotionProgressThrottle throttle(Ms(500));
    // 10 seconds of idle polling at 60fps: zero emissions, and the state
    // must stay coherent (no accumulation, no delayed burst afterwards).
    for (int t = 0; t <= 10000; t += 16) {
        EXPECT_FALSE(throttle.poll(false, at(t)));
    }
}

TEST(MotionProgressThrottleTest, FirstSnapshotDueOneIntervalAfterStart)
{
    MotionProgressThrottle throttle(Ms(500));

    // idle → active edge at t=1000 opens the window; t=0 is motion_started's
    // job, so nothing fires before +500ms.
    EXPECT_FALSE(throttle.poll(false, at(1000)));
    EXPECT_FALSE(throttle.poll(true, at(1000)));   // edge: open window, no emit
    EXPECT_FALSE(throttle.poll(true, at(1499)));   // 499ms in: still inside
    EXPECT_TRUE(throttle.poll(true, at(1500)));    // exactly +500ms: due
    EXPECT_FALSE(throttle.poll(true, at(1501)));   // window restarted at 1500
}

TEST(MotionProgressThrottleTest, AtMostOneEmissionPerIntervalAt60Fps)
{
    MotionProgressThrottle throttle(Ms(500));
    EXPECT_FALSE(throttle.poll(false, at(0)));
    EXPECT_FALSE(throttle.poll(true, at(0)));

    // 60fps polling across 3 seconds (0..3000ms, 16ms grid): the grid never
    // lands on exact 500ms multiples, so emissions land at 512, 1024, 1536,
    // 2048, 2560 — exactly 5, never two inside one 500ms window. This is the
    // anti-storm invariant the spec pins (≤1 per 500ms, never per-frame).
    int emissions = 0;
    for (int t = 16; t <= 3000; t += 16) {
        if (throttle.poll(true, at(t))) {
            ++emissions;
        }
    }
    EXPECT_EQ(emissions, 5);
}

TEST(MotionProgressThrottleTest, IdleGapResetsWindowForNextMotion)
{
    MotionProgressThrottle throttle(Ms(500));

    // Motion A: 0..800ms (one emission at 500).
    EXPECT_FALSE(throttle.poll(false, at(0)));
    EXPECT_FALSE(throttle.poll(true, at(0)));
    int emissions = 0;
    for (int t = 16; t <= 800; t += 16) {
        if (throttle.poll(true, at(t))) ++emissions;
    }
    EXPECT_EQ(emissions, 1);

    // Motion end → idle stretch slides the window...
    for (int t = 816; t <= 2000; t += 16) {
        EXPECT_FALSE(throttle.poll(false, at(t)));
    }

    // Motion B at t=2000: full 500ms headroom again (first snapshot at 2512
    // on the 16ms grid — 512ms after start), proving the window did not leak
    // across the idle gap.
    EXPECT_FALSE(throttle.poll(true, at(2000)));
    emissions = 0;
    for (int t = 2016; t <= 3000; t += 16) {
        if (throttle.poll(true, at(t))) ++emissions;
    }
    EXPECT_EQ(emissions, 1); // at 2512 only — 3012 would be next
}

TEST(MotionProgressThrottleTest, SubIntervalMotionEmitsNothing)
{
    MotionProgressThrottle throttle(Ms(500));
    // A 400ms motion: bracketed by motion_started/motion_finished alone —
    // the progress stream stays silent for short motions by design.
    EXPECT_FALSE(throttle.poll(false, at(0)));
    EXPECT_FALSE(throttle.poll(true, at(0)));
    for (int t = 16; t <= 400; t += 16) {
        EXPECT_FALSE(throttle.poll(true, at(t)));
    }
}

// --- FpsCounter: deterministic EMA over injected frame series --------------

TEST(FpsCounterTest, SentinelUntilFirstValidSample)
{
    Monitor::FpsCounter counter;
    EXPECT_DOUBLE_EQ(counter.fps(), -1.0); // no sample yet → sentinel
}

TEST(FpsCounterTest, ConstantSixtyFpsSeriesConvergesToSixty)
{
    Monitor::FpsCounter counter;
    // 5 seconds of exact 1/60s frames: EMA seeded with 1/60 and never
    // perturbed → exactly 60.0.
    for (int i = 0; i < 300; ++i) {
        counter.onFrame(1.0 / 60.0);
    }
    EXPECT_DOUBLE_EQ(counter.fps(), 60.0);
}

TEST(FpsCounterTest, StepChangeConvergesWithinRoughlyOneWindow)
{
    Monitor::FpsCounter counter(1.0); // ~1s smoothing window
    counter.onFrame(1.0 / 60.0);      // seed at 60fps

    // Instant switch to 30fps (dt = 1/30). Per-step decay is 30/31, i.e. the
    // time constant is ~30 frames = ~1s of wall time: after 1s worth of
    // samples the estimate must be ~63% of the way (≈37fps), after 4s
    // effectively there (≈30.3fps).
    for (int i = 0; i < 30; ++i) counter.onFrame(1.0 / 30.0);
    EXPECT_NEAR(counter.fps(), 30.0, 7.0);
    for (int i = 0; i < 90; ++i) counter.onFrame(1.0 / 30.0);
    EXPECT_NEAR(counter.fps(), 30.0, 0.5);
}

TEST(FpsCounterTest, InvalidFrameDeltasAreIgnored)
{
    Monitor::FpsCounter counter;
    for (int i = 0; i < 120; ++i) counter.onFrame(1.0 / 60.0);
    EXPECT_DOUBLE_EQ(counter.fps(), 60.0);

    counter.onFrame(0.0);   // paused clock / first frame: ignored
    counter.onFrame(-5.0);  // invalid: ignored
    counter.onFrame(std::nan("")); // NaN: ignored
    EXPECT_DOUBLE_EQ(counter.fps(), 60.0); // untouched
}

TEST(FpsCounterTest, SingleStallIsClampedAndRecovers)
{
    Monitor::FpsCounter counter(1.0);
    for (int i = 0; i < 120; ++i) counter.onFrame(1.0 / 60.0);

    // 30s minimized-window stall clamped to tau=1s: one alpha=0.5 step moves
    // the EMA at most halfway toward 1s — fps can never drop below the
    // 1/tau = 1.0 floor from a single stall sample.
    counter.onFrame(30.0);
    EXPECT_GE(counter.fps(), 1.0);
    EXPECT_LT(counter.fps(), 60.0);

    // ~1s window: after 10s of clean 60fps frames the estimate is back.
    for (int i = 0; i < 600; ++i) counter.onFrame(1.0 / 60.0);
    EXPECT_NEAR(counter.fps(), 60.0, 0.5);
}

TEST(FpsCounterTest, JitteredFramesStayBounded)
{
    Monitor::FpsCounter counter;
    // Alternating 50/70fps frames (mean 60fps): the smoothed value must stay
    // between the two rates.
    for (int i = 0; i < 240; ++i) {
        counter.onFrame(i % 2 == 0 ? 1.0 / 50.0 : 1.0 / 70.0);
    }
    EXPECT_GT(counter.fps(), 50.0);
    EXPECT_LT(counter.fps(), 70.0);
}
