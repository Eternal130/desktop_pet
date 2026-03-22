package com.desktoppet.model;

public record WindowConfig(int positionX, int positionY, int width, int height, double opacity) {
    public static WindowConfig defaults() {
        return new WindowConfig(1200, 600, 400, 500, 1.0);
    }
}
