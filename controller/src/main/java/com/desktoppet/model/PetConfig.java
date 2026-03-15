package com.desktoppet.model;

public record PetConfig(
    WindowConfig window,
    ModelSettingsConfig model,
    BehaviorConfig behavior,
    SystemConfig system
) {
    public static PetConfig defaults() {
        return new PetConfig(
            WindowConfig.defaults(),
            ModelSettingsConfig.defaults(),
            BehaviorConfig.defaults(),
            SystemConfig.defaults()
        );
    }
}
