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
    int marginV             // default: 30
) {
    public static SubtitleStyle defaultStyle() {
        return new SubtitleStyle("Microsoft YaHei", 48.0, 0x00FFFFFFL, 0x00000000L, 2.0, 0x00000000L, 0.0, 2, 30);
    }
}
