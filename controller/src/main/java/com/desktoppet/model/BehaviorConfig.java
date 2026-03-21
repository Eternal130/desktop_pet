package com.desktoppet.model;

public record BehaviorConfig(String dragMode, int idleIntervalSeconds, int targetFps) {
    /** targetFps: 0 = adaptive (VSync), 15-120 = fixed frame rate */
    public static BehaviorConfig defaults() {
        return new BehaviorConfig("direct", 10, 0);
    }
}
