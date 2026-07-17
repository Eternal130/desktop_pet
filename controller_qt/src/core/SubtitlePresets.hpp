// allow: SIZE_OK - pure preset data table, 15 presets x 16 fields. Tables of
// data are not logic; the alternative (splitting into per-preset files) would
// scatter a single coherent Java mapPresetToStyle port across 15 files.
#pragma once

#include <QString>
#include <QStringList>

#include "network/Protocol.hpp"  // SubtitleStyle

// SubtitlePresets (todo 21) — the 15 named subtitle style presets, ported
// VERBATIM from Java MainWindowController::mapPresetToStyle (lines 903-986).
// Each preset maps a Chinese display name → a fully-populated SubtitleStyle
// (16 fields). Consumed by:
//   - StartupSalvo (sendSalvo maps InstanceConfig.subtitleStylePreset → style
//     → buildSetSubtitleStyle so the renderer applies the user-chosen preset
//     on every ready→salvo bootstrap, not just the default).
//   - InstanceSession::setSubtitleStylePreset (todo 21 UI setter — ComboBox
//     onActivated → send set_subtitle_style with the preset's style + persist
//     the preset name).
//
// Color byte order: AABBGGRR uint32 (interface.md §1.3 AUTHORITATIVE).
//   - 0x00FFFFFF = 16777215 = opaque white
//   - 0x80000000 = 2147483648 = 50% transparent black
//   - 0xFF...... = fully transparent (alpha=0xFF)
//
// The Java SubtitleStyle.java comment says "RRGGBBTT" but the VALUES only
// make sense under AABBGGRR (verified by the existing Qt Protocol.hpp color()
// helper + the command_all_25.json fixture which the B1 regression guard
// pins). Copied VERBATIM — do NOT reinterpret byte order.
//
// Source order mirrors the Java switch statement (MainWindowController.java:
// 903-986) so a side-by-side audit is line-for-line.
namespace SubtitlePresets {

// The 15 preset names in ComboBox-list order (MainWindowController.java:381).
// "默认" is first — the default selection for a brand-new InstanceConfig
// (InstanceConfig.subtitleStylePreset defaults to "默认").
inline QStringList presetNames()
{
    return {
        QStringLiteral(u"默认"),
        QStringLiteral(u"阴影"),
        QStringLiteral(u"气泡框"),
        QStringLiteral(u"极简"),
        QStringLiteral(u"樱花粉"),
        QStringLiteral(u"赛博霓虹"),
        QStringLiteral(u"星空紫"),
        QStringLiteral(u"橙焰活力"),
        QStringLiteral(u"和风墨韵"),
        QStringLiteral(u"极简投影"),
        QStringLiteral(u"流媒体盒"),
        QStringLiteral(u"毛玻璃"),
        QStringLiteral(u"终端绿"),
        QStringLiteral(u"暗夜卡片"),
        QStringLiteral(u"消息气泡"),
    };
}

// Map a preset name → SubtitleStyle. Empty/null/unknown names return the
// 默认 preset (mirrors Java mapPresetToStyle's `default ->` case). The 默认
// preset reproduces SubtitleStyle{} default member initializers byte-for-byte
// (Protocol.hpp:38-55) so the renderer's ASS style 0 is identical whether the
// controller sends buildSetSubtitleStyle() with no arg or with the 默认 preset.
inline SubtitleStyle mapPresetToStyle(const QString& preset)
{
    // ── 朴素预设（加 edgeBlur 现代化）─────────────────────────────────────
    if (preset == QStringLiteral(u"阴影")) {
        return SubtitleStyle{
            QStringLiteral("Microsoft YaHei"), 48.0, 0x00FFFFFFu, 0x00111111u, 1.8,
            0x00000000u, 3.0, 2, 30.0,
            0.6, -1, 0.5,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"气泡框")) {
        return SubtitleStyle{
            QStringLiteral("Microsoft YaHei"), 48.0, 0x00FFFFFFu, 0x00000000u, 0.0,
            0x99000000u, 2.0, 2, 30.0,
            0.0, -1, 0.0,
            true, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"极简")) {
        return SubtitleStyle{
            QStringLiteral("Microsoft YaHei"), 48.0, 0x00FFFFFFu, 0x00000000u, 0.0,
            0x00000000u, 0.0, 2, 30.0,
            0.0, -1, 0.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }

    // ── 现代二次元预设 ──
    // 颜色为 RRGGBBTT 格式, TT=00 不透明 (Java comment; values are AABBGGRR
    // per interface.md §1.3 — copied verbatim, byte order preserved).
    if (preset == QStringLiteral(u"樱花粉")) {  // 桜色柔光
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 48.0, 0x00FFB7C5u, 0x004A1020u, 1.8,
            0x99FFB6C1u, 2.0, 2, 30.0,
            0.8, 0, 1.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"赛博霓虹")) {  // 霓虹光晕
        return SubtitleStyle{
            QStringLiteral("Orbitron"), 46.0, 0x0000F5FFu, 0x000B0F14u, 2.0,
            0xA600F5FFu, 0.5, 2, 30.0,
            2.5, 1, 2.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"星空紫")) {  // 薰衣草星夜
        return SubtitleStyle{
            QStringLiteral("Source Han Serif CN"), 48.0, 0x00CDB7FFu, 0x001C1630u, 2.0,
            0x8C000000u, 3.0, 2, 30.0,
            0.8, 0, 1.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"橙焰活力")) {  // 琥珀暖阳
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 50.0, 0x00FFB347u, 0x002B1810u, 1.8,
            0x99FF6B00u, 2.0, 2, 30.0,
            0.6, 0, 0.5,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"和风墨韵")) {  // 和風墨韵
        return SubtitleStyle{
            QStringLiteral("Source Han Serif CN"), 46.0, 0x001A1A2Eu, 0x00F5F0E6u, 1.5,
            0xA68B4513u, 1.5, 2, 30.0,
            0.4, 0, 2.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }

    // ── 现代化风格（非二次元）───────────────────────────────────────────
    if (preset == QStringLiteral(u"极简投影")) {
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 50.0, 0x00FAFAFAu, 0x00000000u, 0.0,
            0x33000000u, 3.0, 2, 30.0,
            1.0, 0, 2.0,
            false, 0x80000000u, 12.0, 6.0,
        };
    }
    if (preset == QStringLiteral(u"流媒体盒")) {
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 48.0, 0x00FFFFFFu, 0x00000000u, 0.0,
            0x00000000u, 0.0, 2, 30.0,
            0.5, 0, 1.0,
            true, 0x40000000u, 14.0, 8.0,
        };
    }
    if (preset == QStringLiteral(u"毛玻璃")) {
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 46.0, 0x00FFFFFFu, 0x40FFFFFFu, 2.0,
            0x00000000u, 0.0, 2, 30.0,
            1.5, 0, 0.0,
            true, 0xD0FFFFFFu, 16.0, 10.0,
        };
    }
    if (preset == QStringLiteral(u"终端绿")) {
        return SubtitleStyle{
            QStringLiteral("Consolas"), 42.0, 0x0033FF33u, 0x0033FF33u, 0.0,
            0x6033FF33u, 2.0, 2, 30.0,
            1.5, 0, 1.0,
            true, 0x260B0D10u, 12.0, 8.0,
        };
    }
    if (preset == QStringLiteral(u"暗夜卡片")) {
        return SubtitleStyle{
            QStringLiteral("Noto Sans CJK SC"), 46.0, 0x00E9EEF5u, 0x000B0D10u, 1.0,
            0x660B0D10u, 2.0, 2, 30.0,
            1.0, 1, 0.5,
            true, 0x33151A21u, 14.0, 8.0,
        };
    }
    if (preset == QStringLiteral(u"消息气泡")) {
        return SubtitleStyle{
            QStringLiteral("Microsoft YaHei"), 44.0, 0x00FFFFFFu, 0x00000000u, 0.0,
            0xCC000000u, 0.0, 2, 30.0,
            0.5, 0, 0.0,
            true, 0x003B82F6u, 16.0, 10.0,
        };
    }

    // ── 默认（现代化基线）─────────────────────────────────────────────
    // Reached when preset == "默认" OR preset is unknown/null/empty. Matches
    // Java mapPresetToStyle's `default ->` case AND SubtitleStyle{}'s default
    // member initializers (Protocol.hpp:38-55). Returned by VALUE so callers
    // can mutate without affecting the table.
    return SubtitleStyle{};
}

} // namespace SubtitlePresets
