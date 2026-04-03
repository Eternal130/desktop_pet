package com.desktoppet.model;

public record SystemConfig(boolean autoStart, String defaultGraphicsBackend) {
    public static SystemConfig defaults() {
        return new SystemConfig(false, "opengl");
    }
}
