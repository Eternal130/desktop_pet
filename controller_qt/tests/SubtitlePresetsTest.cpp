// SubtitlePresetsTest -- Phase 5 Wave 8 todo 21 TDD for the subtitle preset
// table ported from Java mapPresetToStyle.
//
// Five behaviors locked:
//   1. testPresetNamesHas15Entries: presetNames() returns exactly 15 names
//      in the ComboBox-list order from MainWindowController.java:381.
//   2. testDefaultPresetMatchesSubtitleStyleDefaults: mapPresetToStyle("默认")
//      == SubtitleStyle{} (the struct's default member initializers). The
//      startup salvo's buildSetSubtitleStyle() with no arg produces the same
//      bytes as buildSetSubtitleStyle(mapPresetToStyle("默认")).
//   3. testAllNamedPresetsAreDistinct: each of the 15 names returns a style
//      where at least one field differs from the default ("默认"). Locks the
//      port's faithfulness -- a missed field would silently make a preset a
//      clone of the default.
//   4. testUnknownPresetReturnsDefault: mapPresetToStyle("nonexistent") ==
//      SubtitleStyle{}. Mirrors Java mapPresetToStyle's `default ->` case.
//   5. testB1RegressionGuardAABBGGRR (B1 GUARD): a representative preset
//      (赛博霓虹) carries the EXACT uint32 color values from Java --
//      0x0000F5FF primary, 0xA600F5FF shadow. These are AABBGGRR per
//      interface.md §1.3. If a future edit reinterprets them as RRGGBBTT,
//      the values would change (e.g. 0x0000F5FF → 0xFFF50000), this test
//      fails, and ProtocolFactoryTest's set_subtitle_style fixture
//      assertion would also catch it. This test is the Belief-1 guard at
//      the preset layer.
//
// QTEST_APPLESS_MAIN: pure data-table queries, no event loop needed.

#include "core/SubtitlePresets.hpp"

#include <QObject>
#include <QTest>

class SubtitlePresetsTest : public QObject
{
    Q_OBJECT

private slots:
    void testPresetNamesHas15Entries();
    void testDefaultPresetMatchesSubtitleStyleDefaults();
    void testAllNamedPresetsAreDistinct();
    void testUnknownPresetReturnsDefault();
    void testB1RegressionGuardAABBGGRR();
};

void SubtitlePresetsTest::testPresetNamesHas15Entries()
{
    const QStringList names = SubtitlePresets::presetNames();
    QCOMPARE(names.size(), 15);
    // First entry is the default selection (InstanceConfig.subtitleStylePreset
    // defaults to "默认").
    QCOMPARE(names.first(), QStringLiteral(u"默认"));
    // Spot-check that all 15 Java preset names are present (no transcription
    // drift). Order is NOT asserted here -- the Java ComboBox addAll order is
    // the only source of truth for order, and Java's is reproducible from the
    // literal in MainWindowController.java:381.
    for (const QString& expected : {
        QStringLiteral(u"默认"), QStringLiteral(u"阴影"), QStringLiteral(u"气泡框"),
        QStringLiteral(u"极简"), QStringLiteral(u"樱花粉"), QStringLiteral(u"赛博霓虹"),
        QStringLiteral(u"星空紫"), QStringLiteral(u"橙焰活力"), QStringLiteral(u"和风墨韵"),
        QStringLiteral(u"极简投影"), QStringLiteral(u"流媒体盒"), QStringLiteral(u"毛玻璃"),
        QStringLiteral(u"终端绿"), QStringLiteral(u"暗夜卡片"), QStringLiteral(u"消息气泡"),
    }) {
        QVERIFY2(names.contains(expected),
                 qPrintable(QStringLiteral("preset name missing: %1").arg(expected)));
    }
}

void SubtitlePresetsTest::testDefaultPresetMatchesSubtitleStyleDefaults()
{
    // "默认" reproduces SubtitleStyle{}'s default member initializers
    // byte-for-byte. This is the B1 contract at the preset layer: a default-
    // constructed SubtitleStyle and mapPresetToStyle("默认") are
    // interchangeable. ProtocolFactoryTest's set_subtitle_style fixture
    // assertion (against command_all_25.json) indirectly pins these same
    // values; this test pins them directly at the preset layer.
    const SubtitleStyle a = SubtitlePresets::mapPresetToStyle(QStringLiteral(u"默认"));
    const SubtitleStyle b{};
    QCOMPARE(a.fontName,         b.fontName);
    QCOMPARE(a.fontSize,         b.fontSize);
    QCOMPARE(a.primaryColor,     b.primaryColor);
    QCOMPARE(a.outlineColor,     b.outlineColor);
    QCOMPARE(a.outlineWidth,     b.outlineWidth);
    QCOMPARE(a.shadowColor,      b.shadowColor);
    QCOMPARE(a.shadowDepth,      b.shadowDepth);
    QCOMPARE(a.alignment,        b.alignment);
    QCOMPARE(a.marginV,          b.marginV);
    QCOMPARE(a.edgeBlur,         b.edgeBlur);
    QCOMPARE(a.fontWeight,       b.fontWeight);
    QCOMPARE(a.letterSpacing,    b.letterSpacing);
    QCOMPARE(a.bgBoxEnabled,     b.bgBoxEnabled);
    QCOMPARE(a.bgBoxColor,       b.bgBoxColor);
    QCOMPARE(a.bgBoxPaddingX,    b.bgBoxPaddingX);
    QCOMPARE(a.bgBoxPaddingY,    b.bgBoxPaddingY);
}

void SubtitlePresetsTest::testAllNamedPresetsAreDistinct()
{
    // Each of the 15 names MUST map to a style that differs from the default
    // in at least one field -- otherwise the preset is a no-op clone. The
    // "默认" name is excluded from this assertion because it IS the default.
    const SubtitleStyle def = SubtitlePresets::mapPresetToStyle(QStringLiteral(u"默认"));
    const QStringList names = SubtitlePresets::presetNames();
    for (const QString& name : names) {
        if (name == QStringLiteral(u"默认"))
            continue;
        const SubtitleStyle s = SubtitlePresets::mapPresetToStyle(name);
        const bool distinct =
            s.fontName != def.fontName ||
            s.fontSize != def.fontSize ||
            s.primaryColor != def.primaryColor ||
            s.outlineColor != def.outlineColor ||
            s.outlineWidth != def.outlineWidth ||
            s.shadowColor != def.shadowColor ||
            s.shadowDepth != def.shadowDepth ||
            s.alignment != def.alignment ||
            s.marginV != def.marginV ||
            s.edgeBlur != def.edgeBlur ||
            s.fontWeight != def.fontWeight ||
            s.letterSpacing != def.letterSpacing ||
            s.bgBoxEnabled != def.bgBoxEnabled ||
            s.bgBoxColor != def.bgBoxColor ||
            s.bgBoxPaddingX != def.bgBoxPaddingX ||
            s.bgBoxPaddingY != def.bgBoxPaddingY;
        QVERIFY2(distinct,
                 qPrintable(QStringLiteral("preset '%1' is identical to 默认 -- "
                                           "port missed a field").arg(name)));
    }
}

void SubtitlePresetsTest::testUnknownPresetReturnsDefault()
{
    // Java mapPresetToStyle's `default ->` case returns the 默认 style for
    // any unrecognized name (including null/empty). The Qt port matches.
    const SubtitleStyle def = SubtitlePresets::mapPresetToStyle(QStringLiteral(u"默认"));
    const SubtitleStyle unknown = SubtitlePresets::mapPresetToStyle(
        QStringLiteral("nonexistent-preset"));
    QCOMPARE(unknown.fontName,      def.fontName);
    QCOMPARE(unknown.primaryColor,  def.primaryColor);
    QCOMPARE(unknown.shadowDepth,   def.shadowDepth);
    QCOMPARE(unknown.bgBoxEnabled,  def.bgBoxEnabled);

    // Empty string also falls through to 默认.
    const SubtitleStyle empty = SubtitlePresets::mapPresetToStyle(QString{});
    QCOMPARE(empty.primaryColor, def.primaryColor);
}

void SubtitlePresetsTest::testB1RegressionGuardAABBGGRR()
{
    // B1 GUARD: the color values are AABBGGRR uint32 (interface.md §1.3
    // AUTHORITATIVE), NOT RRGGBBTT despite the Java comment. Copied VERBATIM
    // from Java MainWindowController.java:929-933 (赛博霓虹 case):
    //   primaryColor = 0x0000F5FF  (opaque cyan -- AABBGGRR: AA=00, BB=00, GG=F5, RR=FF)
    //   outlineColor = 0x000B0F14  (opaque very-dark blue)
    //   shadowColor  = 0xA600F5FF  (mostly-transparent cyan glow)
    //
    // If a future edit reinterprets as RRGGBBTT (treating the high byte as
    // RED), the values would byte-swap (e.g. 0x0000F5FF → 0xFFF50000 in
    // RRGGBBTT interpretation = opaque red-yellow). This test pins the exact
    // bytes so any byte-swap regression fails here, AND ProtocolFactoryTest's
    // set_subtitle_style fixture assertion (against command_all_25.json)
    // would catch it at the wire-format layer too.
    const SubtitleStyle s = SubtitlePresets::mapPresetToStyle(
        QStringLiteral(u"赛博霓虹"));
    QCOMPARE(static_cast<quint64>(s.primaryColor), Q_UINT64_C(0x0000F5FF));
    QCOMPARE(static_cast<quint64>(s.outlineColor), Q_UINT64_C(0x000B0F14));
    QCOMPARE(static_cast<quint64>(s.shadowColor),  Q_UINT64_C(0xA600F5FF));
    // Font name + size are part of the same preset's identity.
    QCOMPARE(s.fontName, QStringLiteral("Orbitron"));
    QCOMPARE(s.fontSize, 46.0);
}

QTEST_APPLESS_MAIN(SubtitlePresetsTest)
#include "SubtitlePresetsTest.moc"
