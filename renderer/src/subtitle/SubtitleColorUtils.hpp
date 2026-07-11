#pragma once

// ============================================================================
// SubtitleColorUtils — header-only pure-logic color conversion helpers.
//
// Extracted from SubtitleManager::ConvertAssImageToTextures() so the math can
// be unit-tested WITHOUT pulling in ass.h / GL / Vulkan.
//
// Two distinct color layouts coexist in libass:
//   - ASS_Image.color      : 0xRRGGBBTT  (TT = transparency at LOW byte)
//   - ASS Style PrimaryColour: 0xAABBGGRR (AA = transparency at HIGH byte)
// In BOTH layouts the alpha byte is TRANSPARENCY: 0x00 = opaque, 0xFF = fully
// transparent.
// ============================================================================

#include <cstdint>
#include <cstddef>

namespace SubtitleColorUtils {

/// Premultiplied-alpha RGBA pixel (matches the blend mode
/// GL_ONE / GL_ONE_MINUS_SRC_ALPHA used by the subtitle overlay quads).
struct RGBA8 {
    uint8_t r, g, b, a;
};

/// Convert an ASS_Image.color value (0xRRGGBBTT) to a premultiplied-alpha
/// RGBA8 pixel, combining the per-style transparency (TT) with the per-pixel
/// glyph coverage (0..255 from the libass single-channel bitmap).
///
/// @param assColor   ASS_Image.color — 0xRRGGBBTT (TT low byte = transparency)
/// @param glyphAlpha per-pixel coverage 0..255 (0 = outside glyph)
/// @return premultiplied RGBA8 (RGB pre-scaled by final alpha)
inline RGBA8 assImageColorToRGBA(uint32_t assColor, uint8_t glyphAlpha) {
    const int styleA = 0xFF - static_cast<int>(assColor & 0xFF);   // TT -> opacity
    const int cR = static_cast<int>((assColor >> 24) & 0xFF);
    const int cG = static_cast<int>((assColor >> 16) & 0xFF);
    const int cB = static_cast<int>((assColor >> 8)  & 0xFF);
    const int finalA = (styleA * glyphAlpha) / 255;
    return RGBA8{
        static_cast<uint8_t>((cR * finalA) / 255),
        static_cast<uint8_t>((cG * finalA) / 255),
        static_cast<uint8_t>((cB * finalA) / 255),
        static_cast<uint8_t>(finalA)
    };
}

/// Convert a controller-side protocol color (0xRRGGBBTT) to the ASS Style
/// color layout (0xAABBGGRR) used by ASS_Style::PrimaryColour etc.
///
/// Controller sends:  RR(bits 24-31) GG(bits 16-23) BB(bits 8-15) TT(bits 0-7)
/// ASS style expects: AA(bits 24-31) BB(bits 16-23) GG(bits 8-15) RR(bits 0-7)
///
/// Both sides use the SAME transparency convention (0x00 = opaque), so the
/// alpha byte maps directly: AA = TT (no inversion). Only the byte ORDER
/// changes (RGB↔BGR swap + alpha repositioning).
///
/// Example: controller opaque white 0xFFFFFF00 -> ASS 0x00FFFFFF.
inline uint32_t protocolColorToASSColor(uint32_t rrggbhtt) {
    const uint8_t rr = (rrggbhtt >> 24) & 0xFF;
    const uint8_t gg = (rrggbhtt >> 16) & 0xFF;
    const uint8_t bb = (rrggbhtt >> 8)  & 0xFF;
    const uint8_t tt = rrggbhtt & 0xFF;
    // ASS AABBGGRR layout — alpha (transparency) is the SAME value as TT.
    return (static_cast<uint32_t>(tt) << 24) |
           (static_cast<uint32_t>(bb) << 16) |
           (static_cast<uint32_t>(gg) << 8)  |
           static_cast<uint32_t>(rr);
}

/// Map an ASS Style color (0xAABBGGRR) back to the controller protocol layout
/// (0xRRGGBBTT). Inverse of protocolColorToASSColor.
inline uint32_t assColorToProtocolColor(uint32_t aabbggrr) {
    const uint8_t aa = (aabbggrr >> 24) & 0xFF;
    const uint8_t bb = (aabbggrr >> 16) & 0xFF;
    const uint8_t gg = (aabbggrr >> 8)  & 0xFF;
    const uint8_t rr = aabbggrr & 0xFF;
    return (static_cast<uint32_t>(rr) << 24) |
           (static_cast<uint32_t>(gg) << 16) |
           (static_cast<uint32_t>(bb) << 8)  |
           static_cast<uint32_t>(aa);
}

} // namespace SubtitleColorUtils
