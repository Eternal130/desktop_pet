package com.desktoppet.model;

public record SystemConfig(boolean autoStart) {
    public static SystemConfig defaults() {
        return new SystemConfig(false);
    }
}
