package com.desktoppet.model;

public record BehaviorConfig(String dragMode, int idleIntervalSeconds) {
    public static BehaviorConfig defaults() {
        return new BehaviorConfig("direct", 10);
    }
}
