#include <ShapeTableCache.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>

namespace {

uint8_t* table(std::initializer_list<uint8_t> v) {
  auto* p = new uint8_t[v.size()];
  std::copy(v.begin(), v.end(), p);
  return p;
}

class ShapeTableCacheTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (const auto& e : ShapeTableCache::entries_) EXPECT_EQ(e.data, nullptr) << "a table outlived its last font";
  }
};

TEST_F(ShapeTableCacheTest, IdenticalBytesShareOneBuffer) {
  uint8_t* a = table({1, 2, 3, 4});
  uint8_t* b = table({1, 2, 3, 4});
  bool shared = true;
  const uint8_t* ta = ShapeTableCache::acquire(a, 4, 1, &shared);
  EXPECT_FALSE(shared);
  EXPECT_EQ(ta, a);
  const uint8_t* tb = ShapeTableCache::acquire(b, 4, 1, &shared);  // frees b
  EXPECT_TRUE(shared);
  EXPECT_EQ(tb, a);
  EXPECT_EQ(ShapeTableCache::refs(ta), 2u);
  ShapeTableCache::release(tb);
  EXPECT_EQ(ShapeTableCache::refs(ta), 1u);
  ShapeTableCache::release(ta);
  EXPECT_EQ(ShapeTableCache::refs(ta), 0u);
}

TEST_F(ShapeTableCacheTest, DifferentBytesLengthOrKindDoNotAlias) {
  uint8_t* base = table({1, 2, 3, 4});
  uint8_t* otherBytes = table({1, 2, 3, 5});
  uint8_t* otherLen = table({1, 2, 3, 4, 0});
  uint8_t* otherKind = table({1, 2, 3, 4});
  bool shared = true;
  const uint8_t* t0 = ShapeTableCache::acquire(base, 4, 1, &shared);
  const uint8_t* t1 = ShapeTableCache::acquire(otherBytes, 4, 1, &shared);
  EXPECT_FALSE(shared);
  const uint8_t* t2 = ShapeTableCache::acquire(otherLen, 5, 1, &shared);
  EXPECT_FALSE(shared);
  const uint8_t* t3 = ShapeTableCache::acquire(otherKind, 4, 2, &shared);
  EXPECT_FALSE(shared);
  EXPECT_NE(t0, t1);
  EXPECT_NE(t0, t2);
  EXPECT_NE(t0, t3);
  EXPECT_EQ(ShapeTableCache::refs(t0), 1u);
  for (const uint8_t* t : {t0, t1, t2, t3}) ShapeTableCache::release(t);
}

TEST_F(ShapeTableCacheTest, NullAndEmptyPassThrough) {
  bool shared = true;
  EXPECT_EQ(ShapeTableCache::acquire(nullptr, 0, 1, &shared), nullptr);
  EXPECT_FALSE(shared);
  ShapeTableCache::release(nullptr);
  uint8_t* empty = table({});
  EXPECT_EQ(ShapeTableCache::acquire(empty, 0, 1), empty);  // not registered
  EXPECT_EQ(ShapeTableCache::refs(empty), 0u);
  ShapeTableCache::release(empty);  // unknown pointer: freed, not leaked
}

TEST_F(ShapeTableCacheTest, TablesBeyondCapacityStayPrivateAndAreFreed) {
  const uint8_t* held[ShapeTableCache::MAX_ENTRIES];
  for (uint8_t i = 0; i < ShapeTableCache::MAX_ENTRIES; ++i) {
    held[i] = ShapeTableCache::acquire(table({i, 9}), 2, 1);
    EXPECT_EQ(ShapeTableCache::refs(held[i]), 1u);
  }
  uint8_t* extra = table({0, 9});  // identical to held[0], but sharing needs a registry hit, which exists
  bool shared = false;
  const uint8_t* t = ShapeTableCache::acquire(extra, 2, 1, &shared);
  EXPECT_TRUE(shared);
  EXPECT_EQ(t, held[0]);
  ShapeTableCache::release(t);
  uint8_t* ninth = table({42, 42});
  const uint8_t* n = ShapeTableCache::acquire(ninth, 2, 1, &shared);
  EXPECT_FALSE(shared);
  EXPECT_EQ(n, ninth);
  EXPECT_EQ(ShapeTableCache::refs(n), 0u);  // registry full: unshared, owned by the caller
  ShapeTableCache::release(n);
  for (const uint8_t* h : held) ShapeTableCache::release(h);
}

}  // namespace
