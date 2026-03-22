package com.desktoppet.model;

/** Runtime state snapshot (immutable). Not persisted directly. */
public record PetState(
    String currentModelName,
    int windowX,
    int windowY,
    int windowWidth,
    int windowHeight,
    boolean connected,
    boolean modelLoaded
) {
    public static PetState initial() {
        return new PetState("", 0, 0, 400, 500, false, false);
    }
}
