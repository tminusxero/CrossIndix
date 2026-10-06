#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontData.h"

// ============================================================================
// Anchor selection and placement for combining marks without GPOS tables.
//
// Hebrew niqqud whose identity depends on position must not use the default
// centre-and-raise heuristic: dagesh sits inside the letter body, the
// shin/sin dots sit over the letter's right/left arm, and holam hangs over
// the left corner (see PR #2541 review feedback).
// ============================================================================

using combiningMark::Anchor;
using combiningMark::anchorFor;
using combiningMark::anchorOver;
using combiningMark::anchorOverRotated90CW;
using combiningMark::raiseAboveBase;

TEST(AnchorFor, PositionSensitiveNiqqud) {
  EXPECT_EQ(anchorFor(0x05BC), Anchor::CenterNative);  // dagesh/mapiq
  EXPECT_EQ(anchorFor(0x05BA), Anchor::CenterNative);  // holam haser for vav
  EXPECT_EQ(anchorFor(0x05C1), Anchor::RightNative);   // shin dot
  EXPECT_EQ(anchorFor(0x05C2), Anchor::LeftNative);    // sin dot
  EXPECT_EQ(anchorFor(0x05B9), Anchor::LeftNative);    // holam
}

// Bengali marks keep their font-designed height: below-base vowel signs and
// the shaped ra-phala hang centred under the base, candrabindu sits centred
// above the headline, and the shaped reph hooks over the base's right edge.
TEST(AnchorFor, BengaliMarksAndShapedClusterMarks) {
  EXPECT_EQ(anchorFor(0x09C1), Anchor::CenterNative);  // vowel sign u
  EXPECT_EQ(anchorFor(0x09C3), Anchor::CenterNative);  // vowel sign vocalic r
  EXPECT_EQ(anchorFor(0x09CD), Anchor::PenNative);     // virama (hasanta): hangs from the stem at the pen
  EXPECT_EQ(anchorFor(0x094D), Anchor::PenNative);     // Devanagari halant
  EXPECT_EQ(anchorFor(0x09BC), Anchor::CenterNative);  // nukta
  EXPECT_EQ(anchorFor(0x0981), Anchor::CenterNative);  // candrabindu
  EXPECT_EQ(anchorFor(0xF000), Anchor::CenterNative);  // PUA below-base mark (ra-phala)
  EXPECT_EQ(anchorFor(0xF0FF), Anchor::CenterNative);
  EXPECT_EQ(anchorFor(0xF100), Anchor::RightNative);  // PUA above-base mark (reph)
  EXPECT_EQ(anchorFor(0xF1FF), Anchor::RightNative);
  EXPECT_EQ(anchorFor(0xF200), Anchor::CenterNative);  // PUA above-base mark, centred (tippi, addak)
  EXPECT_EQ(anchorFor(0xF2FF), Anchor::CenterNative);
  EXPECT_EQ(anchorFor(0xF300), Anchor::RightNative);  // PUA below-base mark at the right edge (subjoined)
  EXPECT_EQ(anchorFor(0xF3FF), Anchor::RightNative);
  EXPECT_EQ(anchorFor(0xE000), Anchor::CenterRaised);  // PUA spacing glyphs are not marks
  EXPECT_EQ(anchorFor(0xF400), Anchor::CenterRaised);  // sign forms
  EXPECT_EQ(anchorFor(0xF7FF), Anchor::CenterRaised);
  EXPECT_EQ(anchorFor(0xF800), Anchor::CenterRaised);  // pre-base consonant forms
}

TEST(AnchorFor, EverythingElseKeepsCentreRaisedDefault) {
  EXPECT_EQ(anchorFor(0x05B0), Anchor::CenterRaised);  // Hebrew sheva
  EXPECT_EQ(anchorFor(0x05B7), Anchor::CenterRaised);  // Hebrew patach
  EXPECT_EQ(anchorFor(0x064E), Anchor::CenterRaised);  // Arabic fatha
  EXPECT_EQ(anchorFor(0x0651), Anchor::CenterRaised);  // Arabic shadda
  EXPECT_EQ(anchorFor(0x0301), Anchor::CenterRaised);  // combining acute
}

// Base glyph: cursor 100, left 1, width 12.  Mark: left 2, width 4.
TEST(AnchorOver, HorizontalPlacementPerAnchor) {
  // Centered: base bitmap spans [101, 113), centre 107; mark starts at 105.
  EXPECT_EQ(anchorOver(Anchor::CenterRaised, 100, 1, 12, 2, 4), 105 - 2);
  EXPECT_EQ(anchorOver(Anchor::CenterNative, 100, 1, 12, 2, 4), 105 - 2);
  // Left-aligned: mark bitmap starts at the base bitmap's left edge (101).
  EXPECT_EQ(anchorOver(Anchor::LeftNative, 100, 1, 12, 2, 4), 101 - 2);
  EXPECT_EQ(anchorOver(Anchor::PenNative, 100, 1, 12, 2, 4, 14), 114);  // pen after the base, own bearing kept
}

TEST(CombiningMarkAnchor, GlyphAnchorsGiveTheFontsOwnPlacement) {
  using namespace glyphAnchor;
  EXPECT_EQ(markOffset(NONE, 130), INT32_MIN);  // CrossInk version 4 file or built-in font: rules apply
  EXPECT_EQ(markOffset(130, NONE), INT32_MIN);
  EXPECT_EQ(markOffset(128 + 40, 128 + 0), 20);    // base anchor 20 px, mark anchor 0
  EXPECT_EQ(markOffset(128 + 41, 128 + 0), 21);    // 20.5 px rounds away from zero
  EXPECT_EQ(markOffset(128 + 10, 128 + 33), -12);  // -11.5 px
  EXPECT_EQ(markOffset(128 - 127, 128 + 127), -127);
  EXPECT_TRUE(combiningMark::attachesBelow(0x094D));   // halant
  EXPECT_TRUE(combiningMark::attachesBelow(0x09CD));   // hasanta
  EXPECT_TRUE(combiningMark::attachesBelow(0x0941));   // u
  EXPECT_TRUE(combiningMark::attachesBelow(0xF000));   // ra-phala / rakar marks
  EXPECT_FALSE(combiningMark::attachesBelow(0x0902));  // anusvara
  EXPECT_FALSE(combiningMark::attachesBelow(0x0981));  // candrabindu
  EXPECT_FALSE(combiningMark::attachesBelow(0xF100));  // reph
  EXPECT_EQ(anchorOverRotated90CW(Anchor::PenNative, 100, 1, 12, 2, 4, 14), 86);
  // Right-aligned: mark bitmap ends at the base bitmap's right edge (113).
  EXPECT_EQ(anchorOver(Anchor::RightNative, 100, 1, 12, 2, 4), 109 - 2);
}

// CrossIndix .cpfont: a mark's other-class byte selects which base point its
// value is measured from; mode 0 keeps the class anchor.
TEST(CombiningMarkAnchor, PlacementModesPickTheBasePoint) {
  using namespace glyphAnchor;
  const uint8_t above = 128 + 40, below = 128 + 10, extra = 128 + 60;  // 20 / 5 / 30 px
  // Above mark, class mode: value in anchorAbove, 0 in anchorBelow.
  EXPECT_EQ(markOffsetWithMode(false, above, below, extra, 17, 128 + 0, CLASS_ANCHOR), 20);
  // Below mark, class mode.
  EXPECT_EQ(markOffsetWithMode(true, above, below, extra, 17, CLASS_ANCHOR, 128 + 4), 3);
  // Pen mode: value is half pixels from the base advance (17 px + 2 px).
  EXPECT_EQ(markOffsetWithMode(true, above, below, extra, 17, PEN, 128 + 4), 19);
  EXPECT_EQ(markOffsetWithMode(false, NONE, NONE, NONE, 17, 128 - 6, PEN), 14);  // needs no base anchor
  // Other class: a below mark measured from the above point.
  EXPECT_EQ(markOffsetWithMode(true, above, below, extra, 17, OTHER_CLASS, 128 + 0), 20);
  // Extra point.
  EXPECT_EQ(markOffsetWithMode(false, above, below, extra, 17, 128 + 2, EXTRA), 29);
  EXPECT_EQ(markOffsetWithMode(false, above, below, NONE, 17, 128 + 2, EXTRA), INT32_MIN);  // v5 base: rules apply
  EXPECT_EQ(markOffsetWithMode(true, above, below, extra, 17, PEN, NONE), INT32_MIN);
}

// The rotated coordinate system inverts every left/width term, so the mark's
// offset from the base cursor must be the exact mirror of the unrotated one.
TEST(AnchorOverRotated90CW, MirrorsUnrotatedOffsets) {
  for (const Anchor anchor : {Anchor::CenterRaised, Anchor::CenterNative, Anchor::LeftNative, Anchor::RightNative}) {
    const int offset = anchorOver(anchor, 100, 1, 12, 2, 4) - 100;
    EXPECT_EQ(anchorOverRotated90CW(anchor, 100, 1, 12, 2, 4), 100 - offset);
  }
}

TEST(RaiseAboveBase, NativeAnchorsKeepFontDesignedHeight) {
  // A dagesh-like dot designed inside the letter body (top 8 of a base whose
  // top is 12) must NOT be hoisted above the letter.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterNative, 8, 3, 12), 0);
  // Shin/sin dots overlapping the letter's top must not be pushed clear.
  EXPECT_EQ(raiseAboveBase(Anchor::RightNative, 13, 3, 12), 0);
  EXPECT_EQ(raiseAboveBase(Anchor::LeftNative, 13, 3, 12), 0);
}

TEST(RaiseAboveBase, CentreRaisedBehaviourUnchanged) {
  // Above-baseline mark colliding with the base: raised to restore a 1px gap.
  // gap = markTop - markHeight - baseTop = 10 - 3 - 12 = -5  ->  raise 6.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 10, 3, 12), 6);
  // Already clear of the base: no raise.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 16, 3, 12), 0);
  // Below-baseline mark (kasra, cedilla): stays at font-native position.
  EXPECT_EQ(raiseAboveBase(Anchor::CenterRaised, 2, 4, 12), 0);
}

TEST(CombiningMarkAnchor, LipiAnchorClassesMapOntoTheRendererEnum) {
  // The Indic marks and the PUA cluster marks come from the Lipi descriptors;
  // Center / Right / Pen map onto the renderer's native anchors and None falls
  // through to the Hebrew rules and the raised default.
  EXPECT_EQ(anchorFor(0xF000), Anchor::CenterNative);  // below-centre mark class
  EXPECT_EQ(anchorFor(0xF100), Anchor::RightNative);   // reph class
  EXPECT_EQ(anchorFor(0xF200), Anchor::CenterNative);  // above-centre class
  EXPECT_EQ(anchorFor(0xF300), Anchor::RightNative);   // below-right class
  EXPECT_EQ(anchorFor(0x0902), Anchor::RightNative);   // Devanagari anusvara at the stem
  EXPECT_EQ(anchorFor(0x0947), Anchor::RightNative);   // vowel sign e
  EXPECT_EQ(anchorFor(0x0941), Anchor::CenterNative);  // vowel sign u
  EXPECT_EQ(anchorFor(0x0981), Anchor::CenterNative);  // Bengali candrabindu
  EXPECT_EQ(anchorFor(0x0983), Anchor::CenterRaised);  // visarga: spacing, renderer default
  EXPECT_TRUE(combiningMark::attachesBelow(0xF3FF));
  EXPECT_FALSE(combiningMark::attachesBelow(0xF400));  // pre-base sign forms are spacing
}
