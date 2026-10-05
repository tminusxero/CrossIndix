#include <gtest/gtest.h>

#include "ScriptUiFontCache.h"

TEST(ScriptUiFontCache, CapacityIsClamped) {
  EXPECT_EQ(ScriptUiFontCache(0).capacity(), 1);
  EXPECT_EQ(ScriptUiFontCache(3).capacity(), 3);
  EXPECT_EQ(ScriptUiFontCache(99).capacity(), ScriptUiFontCache::kMaxEntries);
}

TEST(ScriptUiFontCache, AcquireCreatesAndTouchFinds) {
  ScriptUiFontCache cache(2);
  ScriptUiFontCache::Entry evicted;
  auto& bengali = cache.acquire("NotoSerifBengali", evicted);
  EXPECT_FALSE(evicted.used);
  bengali.fontIds[2] = 42;
  EXPECT_EQ(cache.residentCount(), 1);

  auto* found = cache.touch("NotoSerifBengali");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->fontIds[2], 42);
  EXPECT_EQ(cache.touch("Missing"), nullptr);
  EXPECT_EQ(cache.touch(""), nullptr);
  EXPECT_EQ(cache.touch(nullptr), nullptr);
}

TEST(ScriptUiFontCache, EvictsLeastRecentlyUsedAtCap) {
  ScriptUiFontCache cache(2);
  ScriptUiFontCache::Entry evicted;
  cache.acquire("A", evicted).fontIds[0] = 1;
  cache.acquire("B", evicted).fontIds[0] = 2;
  EXPECT_FALSE(evicted.used);
  EXPECT_EQ(cache.residentCount(), 2);

  cache.touch("A");  // B is now the least recently used
  cache.acquire("C", evicted).fontIds[0] = 3;
  ASSERT_TRUE(evicted.used);
  EXPECT_STREQ(evicted.family, "B");
  EXPECT_EQ(evicted.fontIds[0], 2);
  EXPECT_EQ(cache.residentCount(), 2);
  EXPECT_NE(cache.touch("A"), nullptr);
  EXPECT_NE(cache.touch("C"), nullptr);
  EXPECT_EQ(cache.touch("B"), nullptr);
}

TEST(ScriptUiFontCache, CapOneReplacesEveryTime) {
  ScriptUiFontCache cache(1);
  ScriptUiFontCache::Entry evicted;
  cache.acquire("Bengali", evicted).fontIds[1] = 7;
  cache.acquire("Devanagari", evicted);
  ASSERT_TRUE(evicted.used);
  EXPECT_STREQ(evicted.family, "Bengali");
  EXPECT_EQ(evicted.fontIds[1], 7);
  // Re-acquiring the resident family evicts nothing.
  cache.acquire("Devanagari", evicted);
  EXPECT_FALSE(evicted.used);
}

TEST(ScriptUiFontCache, ReleaseAllReportsResidents) {
  ScriptUiFontCache cache(3);
  ScriptUiFontCache::Entry evicted;
  cache.acquire("A", evicted);
  cache.acquire("B", evicted);
  ScriptUiFontCache::Entry out[ScriptUiFontCache::kMaxEntries];
  EXPECT_EQ(cache.releaseAll(out), 2);
  EXPECT_EQ(cache.residentCount(), 0);
  EXPECT_EQ(cache.touch("A"), nullptr);
  auto dropped = cache.release("Z");
  EXPECT_FALSE(dropped.used);
}

TEST(ScriptUiFontCache, ReleaseAllExceptKeepsTheReaderFamily) {
  ScriptUiFontCache cache(2);
  ScriptUiFontCache::Entry evicted;
  cache.acquire("NotoSerifBengali", evicted).fontIds[1] = 7;
  cache.acquire("NotoSerifDevanagari", evicted).fontIds[1] = 9;
  ScriptUiFontCache::Entry out[ScriptUiFontCache::kMaxEntries];
  // Reading a Hindi book: the Bengali family kept for the library list goes.
  ASSERT_EQ(cache.releaseAllExcept("NotoSerifDevanagari", out), 1);
  EXPECT_STREQ(out[0].family, "NotoSerifBengali");
  EXPECT_EQ(out[0].fontIds[1], 7);
  ASSERT_NE(cache.touch("NotoSerifDevanagari"), nullptr);
  EXPECT_EQ(cache.touch("NotoSerifBengali"), nullptr);
  EXPECT_EQ(cache.residentCount(), 1);
  // A built-in reader font (no SD family): everything goes.
  EXPECT_EQ(cache.releaseAllExcept("", out), 1);
  EXPECT_EQ(cache.residentCount(), 0);
  // Nothing resident: nothing to drop, and the freed slot is reused.
  EXPECT_EQ(cache.releaseAllExcept(nullptr, out), 0);
  cache.acquire("NotoSerifBengali", evicted);
  EXPECT_FALSE(evicted.used);
}

TEST(ScriptUiFontCache, LeastRecentlyDrawnSizeGoesFirst) {
  ScriptUiFontCache cache(ScriptUiFontCache::kMaxEntries);
  ScriptUiFontCache::Entry evicted;
  auto& bengali = cache.acquire("Bengali", evicted);
  bengali.fontIds[0] = 11;
  cache.touchSize(bengali, 0);
  bengali.fontIds[1] = 12;
  cache.touchSize(bengali, 1);
  auto& tamil = cache.acquire("Tamil", evicted);
  tamil.fontIds[1] = 21;
  cache.touchSize(tamil, 1);
  cache.touchSize(*cache.touch("Bengali"), 1);  // Bengali 10 pt drawn again

  ScriptUiFontCache::Victim victim;
  ASSERT_TRUE(cache.leastRecentlyUsedSize("Tamil", 1, victim));
  EXPECT_STREQ(victim.family, "Bengali");  // the unused 8 pt size, not a whole family
  EXPECT_EQ(victim.sizeIndex, 0);
  EXPECT_EQ(victim.fontId, 11);
  cache.clearSize(victim.family, victim.sizeIndex);
  ASSERT_NE(cache.peek("Bengali"), nullptr);

  // Next: Tamil is older than Bengali 10 pt, but it is the size about to be drawn.
  ASSERT_TRUE(cache.leastRecentlyUsedSize("Tamil", 1, victim));
  EXPECT_STREQ(victim.family, "Bengali");
  EXPECT_EQ(victim.sizeIndex, 1);
  cache.clearSize(victim.family, victim.sizeIndex);
  EXPECT_EQ(cache.peek("Bengali"), nullptr);  // no size left: the family goes
  EXPECT_FALSE(cache.leastRecentlyUsedSize("Tamil", 1, victim));
  EXPECT_EQ(cache.residentCount(), 1);
}

TEST(ScriptUiFontCache, HoldsTenScriptFamiliesUpToItsSlots) {
  ScriptUiFontCache cache(ScriptUiFontCache::kMaxEntries);
  ScriptUiFontCache::Entry evicted;
  const char* names[] = {"S0", "S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9"};
  int evictions = 0;
  for (const char* n : names) {
    cache.acquire(n, evicted);
    evictions += evicted.used ? 1 : 0;
  }
  EXPECT_EQ(cache.residentCount(), ScriptUiFontCache::kMaxEntries);
  EXPECT_EQ(evictions, 10 - ScriptUiFontCache::kMaxEntries);
}
