#include <gtest/gtest.h>

#include "ScriptBlock.h"

TEST(ScriptBlock, IndicBlocksAreConsecutive) {
  EXPECT_EQ(scriptBlockOf(0x0900), ScriptBlock::Devanagari);
  EXPECT_EQ(scriptBlockOf(0x097F), ScriptBlock::Devanagari);
  EXPECT_EQ(scriptBlockOf(0x0980), ScriptBlock::Bengali);
  EXPECT_EQ(scriptBlockOf(0x09FF), ScriptBlock::Bengali);
  EXPECT_EQ(scriptBlockOf(0x0A00), ScriptBlock::Gurmukhi);
  EXPECT_EQ(scriptBlockOf(0x0A80), ScriptBlock::Gujarati);
  EXPECT_EQ(scriptBlockOf(0x0B00), ScriptBlock::Odia);
  EXPECT_EQ(scriptBlockOf(0x0B80), ScriptBlock::Tamil);
  EXPECT_EQ(scriptBlockOf(0x0C00), ScriptBlock::Telugu);
  EXPECT_EQ(scriptBlockOf(0x0C80), ScriptBlock::Kannada);
  EXPECT_EQ(scriptBlockOf(0x0D00), ScriptBlock::Malayalam);
  EXPECT_EQ(scriptBlockOf(0x0D80), ScriptBlock::Sinhala);
  EXPECT_EQ(scriptBlockOf(0x0DFF), ScriptBlock::Sinhala);
  EXPECT_EQ(scriptBlockOf(0x0E00), ScriptBlock::None);  // Thai is not handled
  EXPECT_EQ(scriptBlockOf(0x08FF), ScriptBlock::None);
}

TEST(ScriptBlock, ProbesMapBackToTheirBlock) {
  for (uint8_t b = 1; b < static_cast<uint8_t>(ScriptBlock::COUNT); b++) {
    const auto block = static_cast<ScriptBlock>(b);
    EXPECT_EQ(scriptBlockOf(scriptProbe(block)), block) << "block " << int(b);
  }
  EXPECT_EQ(scriptProbe(ScriptBlock::None), 0u);
}

TEST(ScriptBlock, CjkRanges) {
  EXPECT_EQ(scriptBlockOf(0x4E2D), ScriptBlock::Han);
  EXPECT_EQ(scriptBlockOf(0x3400), ScriptBlock::Han);
  EXPECT_EQ(scriptBlockOf(0x20000), ScriptBlock::Han);
  EXPECT_EQ(scriptBlockOf(0x3001), ScriptBlock::Han);  // ideographic comma
  EXPECT_EQ(scriptBlockOf(0xFF21), ScriptBlock::Han);  // fullwidth A
  EXPECT_EQ(scriptBlockOf(0x3042), ScriptBlock::Kana);
  EXPECT_EQ(scriptBlockOf(0x30A2), ScriptBlock::Kana);
  EXPECT_EQ(scriptBlockOf(0xFF76), ScriptBlock::Kana);  // halfwidth katakana
  EXPECT_EQ(scriptBlockOf(0xAC00), ScriptBlock::Hangul);
  EXPECT_EQ(scriptBlockOf(0x1100), ScriptBlock::Hangul);
  EXPECT_EQ(scriptBlockOf(0x3131), ScriptBlock::Hangul);
}

TEST(ScriptBlock, LatinAndOthersAreNone) {
  for (uint32_t cp = 0x20; cp < 0x0900; cp++) EXPECT_EQ(scriptBlockOf(cp), ScriptBlock::None) << cp;
  EXPECT_EQ(scriptBlockOf(0x05D0), ScriptBlock::None);  // Hebrew alef
  EXPECT_EQ(scriptBlockOf(0x0627), ScriptBlock::None);  // Arabic alef
  EXPECT_EQ(scriptBlockOf(0x0416), ScriptBlock::None);  // Cyrillic
  EXPECT_EQ(scriptBlockOf(0x2019), ScriptBlock::None);  // punctuation
  EXPECT_EQ(scriptBlockOf(0xE000), ScriptBlock::None);  // PUA composites
  EXPECT_EQ(scriptBlockOf(0xFFFD), ScriptBlock::None);
  EXPECT_FALSE(utf8IsUiFallbackScript('A'));
  EXPECT_TRUE(utf8IsUiFallbackScript(0x0995));
}

TEST(ScriptBlock, DandaIsSharedPunctuation) {
  EXPECT_TRUE(isIndicDanda(0x0964));
  EXPECT_TRUE(isIndicDanda(0x0965));
  EXPECT_FALSE(isIndicDanda(0x0966));
  EXPECT_EQ(scriptBlockOf(0x0964), ScriptBlock::Devanagari);
}
