package com.desktoppet.model;

public record InstanceState(
    String label,
    String model,
    String rendererPath,
    double opacity,
    String dragMode,
    int idleInterval,
    int posX,
    int posY,
    boolean autoStart,
    String currentExpression,
    int targetFps
) {
    public static InstanceState defaults() {
        return new InstanceState(
            "新实例", "", "", 1.0, "direct", 10, 1200, 600, false, "F01", 0
        );
    }
}
