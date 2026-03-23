package com.desktoppet.model;

public record WindowConfig(int positionX, int positionY, int width, int height, double opacity,
                           double layoutOffsetX, double layoutOffsetY, double layoutScale) {
    public static WindowConfig defaults() {
        return new WindowConfig(1200, 600, 400, 500, 1.0, 0.0, 0.0, 1.0);
    }

    public WindowConfig withLayout(double offsetX, double offsetY, double scale) {
        return new WindowConfig(positionX, positionY, width, height, opacity, offsetX, offsetY, scale);
    }
}
