package com.desktoppet.model;

/**
 * Subtitle styling parameters for ASS format rendering.
 * Colors are in RRGGBBTT format (uint32) where TT is transparency (0=opaque, 255=transparent).
 * The renderer converts this to ASS AABBGGRR format internally.
 */
public record SubtitleStyle(
    String fontName,        // default: "Microsoft YaHei"
    double fontSize,        // default: 48.0
    long primaryColor,      // default: 0x00FFFFFF (opaque white, RRGGBBTT format)
    long outlineColor,      // default: 0x00000000 (opaque black)
    double outlineWidth,    // default: 2.0
    long shadowColor,       // default: 0x00000000
    double shadowDepth,     // default: 0.0 (no shadow)
    int alignment,          // default: 2 (ASS numpad bottom-center)
    int marginV,            // default: 30
    // — Phase 1: modern text effects —
    double edgeBlur,        // \blur<N> Gaussian blur on text edges. 0 = disabled.
    int fontWeight,         // -1=don't inject, 0=normal, 1=bold
    double letterSpacing,   // \fsp<N> letter spacing in pixels. 0 = disabled.
    // — Phase 2: background box —
    boolean bgBoxEnabled,   // Draw a translucent box behind text.
    long bgBoxColor,        // RRGGBBTT format, default 0x80000000L (50% opaque black)
    double bgBoxPaddingX,   // Horizontal padding in pixels.
    double bgBoxPaddingY    // Vertical padding in pixels.
) {
    public static SubtitleStyle defaultStyle() {
        return new SubtitleStyle(
            "Microsoft YaHei", 48.0, 0x00FFFFFFL, 0x00111111L, 1.8,
            0x00000000L, 0.0, 2, 30,
            0.6, -1, 0.5,
            false, 0x80000000L, 12.0, 6.0
        );
    }
}
