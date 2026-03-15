package com.desktoppet.model;

/** Runtime state snapshot (immutable). Not persisted directly. */
public record PetState(
    String currentModelName,  // Short name like "Hiyori"
    int windowX,
    int windowY,
    boolean connected,
    boolean modelLoaded
) {
    public static PetState initial() {
        return new PetState("", 0, 0, false, false);
    }
}
