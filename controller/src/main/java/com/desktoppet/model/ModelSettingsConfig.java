package com.desktoppet.model;

public record ModelSettingsConfig(String currentModelName, double scale) {
    public static ModelSettingsConfig defaults() {
        return new ModelSettingsConfig("Hiyori", 1.0);
    }
}
