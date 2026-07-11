// ============================================================================
// SubtitleColorTest — unit tests for SubtitleColorUtils.
//
// Pure-logic tests: NO libass, NO GL, NO Vulkan. Only exercises the color
// conversion math extracted from SubtitleManager::ConvertAssImageToTextures()
// and the protocol→ASS style color mapping.
// ============================================================================

#include <gtest/gtest.h>

#include "subtitle/SubtitleColorUtils.hpp"

#include <cstdint>

using SubtitleColorUtils::RGBA8;
using SubtitleColorUtils::assImageColorToRGBA;
using SubtitleColorUtils::protocolColorToASSColor;
using SubtitleColorUtils::assColorToProtocolColor;

// ==========================================================================
// assImageColorToRGBA — ASS_Image.color (0xRRGGBBTT) → premultiplied RGBA8
//
// Recall: ASS_Image.color byte layout (MSB→LSB): RR GG BB TT
//   TT (low byte) = transparency (0x00 = opaque, 0xFF = fully transparent)
// ==========================================================================

// White, fully opaque (TT=0x00), full glyph coverage → opaque white premult.
TEST(AssImageColorToRGBA, WhiteOpaqueFullAlpha) {
    // 0xFFFFFF00: RR=FF GG=FF BB=FF TT=00
    RGBA8 r = assImageColorToRGBA(0xFFFFFF00u, 255);
    EXPECT_EQ(255, r.r);
    EXPECT_EQ(255, r.g);
    EXPECT_EQ(255, r.b);
    EXPECT_EQ(255, r.a);
}

// Fully transparent style (TT=0xFF) → zero alpha regardless of glyph coverage.
TEST(AssImageColorToRGBA, FullyTransparentStyle) {
    // 0x000000FF: RR=00 GG=00 BB=00 TT=FF
    RGBA8 r = assImageColorToRGBA(0x000000FFu, 255);
    EXPECT_EQ(0, r.a);
    EXPECT_EQ(0, r.r);
    EXPECT_EQ(0, r.g);
    EXPECT_EQ(0, r.b);
}

// Red half-transparent (TT=0x80) with glyph coverage 128.
// styleA = 255 - 128 = 127;  finalA = (127*128)/255 = 63;
// r = (255*63)/255 = 63.
TEST(AssImageColorToRGBA, RedHalfTransparentHalfGlyph) {
    // 0xFF000080: RR=FF GG=00 BB=00 TT=80
    RGBA8 r = assImageColorToRGBA(0xFF000080u, 128);
    EXPECT_EQ(63, r.a);
    EXPECT_EQ(63, r.r);
    EXPECT_EQ(0,  r.g);
    EXPECT_EQ(0,  r.b);
}

// Zero glyph coverage → all-zero premultiplied pixel.
TEST(AssImageColorToRGBA, ZeroGlyphAlpha) {
    RGBA8 r = assImageColorToRGBA(0xFFFFFF00u, 0);
    EXPECT_EQ(0, r.a);
    EXPECT_EQ(0, r.r);
    EXPECT_EQ(0, r.g);
    EXPECT_EQ(0, r.b);
}

// Green opaque with half glyph coverage → G scaled by coverage.
// styleA = 255;  finalA = (255*128)/255 = 128;  g = (255*128)/255 = 128.
TEST(AssImageColorToRGBA, GreenOpaqueHalfGlyph) {
    // 0x00FF0000: RR=00 GG=FF BB=00 TT=00
    RGBA8 r = assImageColorToRGBA(0x00FF0000u, 128);
    EXPECT_EQ(128, r.a);
    EXPECT_EQ(0,   r.r);
    EXPECT_EQ(128, r.g);
    EXPECT_EQ(0,   r.b);
}

// Blue, 75% transparent style (TT=0x40=64), full glyph.
// styleA = 255-64 = 191;  finalA = (191*255)/255 = 191;  b = (255*191)/255 = 191.
TEST(AssImageColorToRGBA, BlueQuarterTransparentFullGlyph) {
    // 0x0000FF40: RR=00 GG=00 BB=FF TT=40
    RGBA8 r = assImageColorToRGBA(0x0000FF40u, 255);
    EXPECT_EQ(191, r.a);
    EXPECT_EQ(0,   r.r);
    EXPECT_EQ(0,   r.g);
    EXPECT_EQ(191, r.b);
}

// Black opaque text — common subtitle outline color — full glyph.
TEST(AssImageColorToRGBA, BlackOpaqueFullGlyph) {
    // 0x00000000: RR=00 GG=00 BB=00 TT=00 (opaque black)
    RGBA8 r = assImageColorToRGBA(0x00000000u, 255);
    EXPECT_EQ(255, r.a);
    EXPECT_EQ(0, r.r);
    EXPECT_EQ(0, r.g);
    EXPECT_EQ(0, r.b);
}

// ==========================================================================
// protocolColorToASSColor — controller 0xRRGGBBTT → ASS Style 0xAABBGGRR
//
// Both layouts use the SAME transparency convention (0x00 = opaque), so the
// alpha byte maps directly (AA = TT, no inversion). Only byte order changes.
// ==========================================================================

// White opaque: controller 0xFFFFFF00 → ASS 0x00FFFFFF (SubtitleStyle default).
TEST(ProtocolColorToASSColor, WhiteOpaque) {
    EXPECT_EQ(0x00FFFFFFu, protocolColorToASSColor(0xFFFFFF00u));
}

// Black opaque: controller 0x00000000 → ASS 0x00000000.
TEST(ProtocolColorToASSColor, BlackOpaque) {
    EXPECT_EQ(0x00000000u, protocolColorToASSColor(0x00000000u));
}

// Red opaque: controller RR=FF GG=00 BB=00 TT=00 → ASS AA=00 BB=00 GG=00 RR=FF.
TEST(ProtocolColorToASSColor, RedOpaque) {
    EXPECT_EQ(0x000000FFu, protocolColorToASSColor(0xFF000000u));
}

// Semi-transparent blue: controller RR=00 GG=00 BB=FF TT=80
// → ASS AA=80 BB=FF GG=00 RR=00 = 0x80FF0000.
TEST(ProtocolColorToASSColor, SemiTransparentBlue) {
    EXPECT_EQ(0x80FF0000u, protocolColorToASSColor(0x0000FF80u));
}

// Fully transparent arbitrary color: controller TT=FF → ASS AA=FF.
TEST(ProtocolColorToASSColor, FullyTransparent) {
    EXPECT_EQ(0xFF563412u, protocolColorToASSColor(0x123456FFu));
}

// ==========================================================================
// assColorToProtocolColor — inverse mapping (ASS Style → controller protocol)
// ==========================================================================

// Inverse of WhiteOpaque.
TEST(AssColorToProtocolColor, WhiteOpaque) {
    EXPECT_EQ(0xFFFFFF00u, assColorToProtocolColor(0x00FFFFFFu));
}

// Round-trip: protocol → ASS → protocol should be identity.
TEST(ColorRoundTrip, ProtocolToASSToProtocol) {
    const uint32_t inputs[] = {
        0xFFFFFF00u, // opaque white
        0x00000000u, // opaque black
        0xFF000000u, // opaque red
        0x00FF0080u, // half-transparent green
        0x123456FFu, // fully transparent
    };
    for (uint32_t in : inputs) {
        uint32_t ass = protocolColorToASSColor(in);
        uint32_t back = assColorToProtocolColor(ass);
        EXPECT_EQ(in, back) << "Round-trip failed for input 0x" << std::hex << in;
    }
}

// Round-trip: ASS → protocol → ASS should be identity.
TEST(ColorRoundTrip, ASSToProtocolToASS) {
    const uint32_t inputs[] = {
        0x00FFFFFFu, // opaque white
        0x00000000u, // opaque black
        0x000000FFu, // opaque red
        0x80FF0000u, // semi-transparent blue
        0xFF563412u, // fully transparent
    };
    for (uint32_t in : inputs) {
        uint32_t proto = assColorToProtocolColor(in);
        uint32_t back = protocolColorToASSColor(proto);
        EXPECT_EQ(in, back) << "Round-trip failed for input 0x" << std::hex << in;
    }
}

// ==========================================================================
// Style default mapping — verifies the SubtitleStyle default colors match the
// expected controller-side protocol values.
// ==========================================================================

// Default primary color: opaque white.
// Controller sends 0xFFFFFF00 (RRGGBBTT) → ASS 0x00FFFFFF (AABBGGRR).
TEST(StyleDefaults, PrimaryColorIsOpaqueWhite) {
    const uint32_t controllerPrimary = 0xFFFFFF00u;
    const uint32_t assPrimary = protocolColorToASSColor(controllerPrimary);
    EXPECT_EQ(0x00FFFFFFu, assPrimary); // matches SubtitleStyle::primaryColor default
}

// Default outline color: opaque black.
// Controller sends 0x00000000 → ASS 0x00000000.
TEST(StyleDefaults, OutlineColorIsOpaqueBlack) {
    const uint32_t controllerOutline = 0x00000000u;
    const uint32_t assOutline = protocolColorToASSColor(controllerOutline);
    EXPECT_EQ(0x00000000u, assOutline); // matches SubtitleStyle::outlineColor default
}
