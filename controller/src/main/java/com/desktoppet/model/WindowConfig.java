package com.desktoppet.model;

public record WindowConfig(int positionX, int positionY, double opacity) {
    public static WindowConfig defaults() {
        return new WindowConfig(1200, 600, 1.0);
    }
}
