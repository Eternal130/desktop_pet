package com.desktoppet.model;

import java.util.UUID;

public record InstanceConfig(
    String id,
    String label,
    String rendererPath,
    String graphicsBackend,
    String modelName,
    double modelScale,
    int windowX,
    int windowY,
    int windowWidth,
    int windowHeight,
    double opacity,
    String dragMode,
    int idleInterval,
    int targetFps,
    boolean autoStart,
    String currentExpression,
    String voicePack,
    double volume,
    boolean muted,
    double layoutOffsetX,
    double layoutOffsetY,
    double layoutScale
) {
    public static InstanceConfig defaults() {
        return new InstanceConfig(
            UUID.randomUUID().toString(),
            "新实例",
            "",
            "opengl",
            "",
            1.0,
            1200, 600, 400, 500,
            1.0,
            "direct",
            10,
            0,
            false,
            "F01",
            null,
            1.0,
            false,
            0.0, 0.0, 1.0
        );
    }

    public static InstanceConfig create(String label, String rendererPath) {
        InstanceConfig d = defaults();
        return new InstanceConfig(
            UUID.randomUUID().toString(),
            label,
            rendererPath,
            d.graphicsBackend(),
            d.modelName(),
            d.modelScale(),
            d.windowX(), d.windowY(), d.windowWidth(), d.windowHeight(),
            d.opacity(),
            d.dragMode(),
            d.idleInterval(),
            d.targetFps(),
            d.autoStart(),
            d.currentExpression(),
            d.voicePack(),
            d.volume(),
            d.muted(),
            d.layoutOffsetX(), d.layoutOffsetY(), d.layoutScale()
        );
    }
}
