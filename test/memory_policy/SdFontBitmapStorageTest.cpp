#include <Arduino.h>
#include <HalStorage.h>
#include <Lipi.h>
#include <SdCardFont.h>
#include <ShapeTableCache.h>
#include <gtest/gtest.h>

#include <cstring>

namespace {
void put16(std::vector<uint8_t>& b, size_t o, uint16_t v) {
  b[o] = v;
  b[o + 1] = v >> 8;
}
void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  put16(b, o, v);
  put16(b, o + 2, v >> 16);
}
template <class T>
void append(std::vector<uint8_t>& b, const T& v) {
  const auto* p = reinterpret_cast<const uint8_t*>(&v);
  b.insert(b.end(), p, p + sizeof(v));
}
std::vector<uint8_t> fixture() {
  std::vector<uint8_t> b(64, 0);
  memcpy(b.data(), "CPFONT\0\0", 8);
  put16(b, 8, CPFONT_VERSION);
  b[12] = 1;
  put32(b, 36, 2);
  put32(b, 40, 4);
  b[44] = 16;
  put16(b, 45, 12);
  put16(b, 47, uint16_t(-4));
  put32(b, 56, 64);
  append(b, EpdUnicodeInterval{'A', 'C', 0});
  append(b, EpdUnicodeInterval{0xFFFD, 0xFFFD, 3});
  for (uint32_t i = 0; i < 4; i++) {
    EpdGlyph g{};
    g.width = 8;
    g.height = 8;
    g.advanceX = 128;
    g.dataLength = 8;
    g.dataOffset = i * 8;
    append(b, g);
  }
  for (uint8_t i = 0; i < 32; i++) b.push_back(i);
  return b;
}
}  // namespace
struct SdFontBitmapStorageTest : testing::Test {
  void SetUp() override {
    fakeheap::reset();
    Storage.reset();
    Storage.put("font.cpfont", fixture());
  }
  void TearDown() override {
    EXPECT_TRUE(fakeheap::live.empty());
    Storage.reset();
  }
};
TEST_F(SdFontBitmapStorageTest, LoadsUpstreamAndOwnFormatVersionsOnly) {
  // CrossInk's version 4 and the CrossIndix format load; the numbers in
  // between (CrossInk's future, and this project's pre-release formats) do not.
  for (const uint16_t version : {uint16_t{CPFONT_UPSTREAM_VERSION}, uint16_t{CPFONT_VERSION}}) {
    auto bytes = fixture();
    put16(bytes, 8, version);
    Storage.put("v.cpfont", bytes);
    SdCardFont font;
    EXPECT_TRUE(font.load("v.cpfont")) << "version " << version;
  }
  for (const uint16_t version : {uint16_t{3}, uint16_t{5}, uint16_t{6}, uint16_t{127}, uint16_t{129}}) {
    auto bytes = fixture();
    put16(bytes, 8, version);
    Storage.put("v.cpfont", bytes);
    SdCardFont font;
    EXPECT_FALSE(font.load("v.cpfont")) << "version " << version;
  }
}

TEST_F(SdFontBitmapStorageTest, ProductionLoadGrowRetainReleaseAndReload) {
  SdCardFont font;
  ASSERT_TRUE(font.load("font.cpfont"));
  ASSERT_EQ(font.prewarm("A", 1), 0);
  auto* data = font.getEpdFont()->data;
  ASSERT_NE(data->bitmap, nullptr);
  EXPECT_EQ(byteBufferPool(data->bitmap), MemoryPool::Psram);
  EXPECT_EQ(memcmp(data->bitmap, fixture().data() + 64 + 24 + 4 * sizeof(EpdGlyph), 8), 0);
  const auto* first = data->bitmap;
  font.clearCache();
  ASSERT_EQ(font.prewarm("A", 1), 0);
  EXPECT_EQ(font.getEpdFont()->data->bitmap, first);
  ASSERT_EQ(font.prewarm("ABC", 1), 0);
  data = font.getEpdFont()->data;
  EXPECT_EQ(byteBufferPool(data->bitmap), MemoryPool::Psram);
  for (int i = 0; i < 32; i++) EXPECT_EQ(data->bitmap[i], i);
  font.releaseForLowMemory();
  EXPECT_TRUE(fakeheap::live.empty());
  EXPECT_EQ(font.getEpdFont()->data->bitmap, nullptr);
  ASSERT_EQ(font.prewarm("B", 1), 0);
  EXPECT_NE(font.getEpdFont()->data->bitmap, nullptr);
  ASSERT_TRUE(font.load("font.cpfont"));
  EXPECT_TRUE(fakeheap::live.empty());
}
TEST_F(SdFontBitmapStorageTest, FailureInvalidatesViewsAndCanRecover) {
  SdCardFont font;
  ASSERT_TRUE(font.load("font.cpfont"));
  ASSERT_EQ(font.prewarm("A", 1), 0);
  fakeheap::external.fail = 1;
  fakeheap::internal.largest = 1;
  EXPECT_GT(font.prewarm("ABC", 1), 0);
  EXPECT_TRUE(font.lastPrewarmFailed());
  EXPECT_EQ(font.getEpdFont()->data->bitmap, nullptr);
  EXPECT_TRUE(fakeheap::live.empty());
  fakeheap::internal.largest = 1024 * 1024;
  ASSERT_EQ(font.prewarm("ABC", 1), 0);
  EXPECT_NE(font.getEpdFont()->data->bitmap, nullptr);
}
TEST_F(SdFontBitmapStorageTest, C3AndAdmittedInternalFallbackAndMetadataOnly) {
  for (bool psram : {false, true}) {
    fakeheap::reset(psram);
    SdCardFont font;
    ASSERT_TRUE(font.load("font.cpfont"));
    ASSERT_EQ(font.prewarm("A", 1, true), 0);
    EXPECT_TRUE(fakeheap::live.empty());
    EXPECT_EQ(font.getEpdFont()->data->bitmap, nullptr);
    fakeheap::external.fail = 1;
    ASSERT_EQ(font.prewarm("ABC", 1), 0);
    EXPECT_EQ(byteBufferPool(font.getEpdFont()->data->bitmap), MemoryPool::Internal);
    font.releaseForLowMemory();
    EXPECT_TRUE(fakeheap::live.empty());
  }
}

TEST_F(SdFontBitmapStorageTest, ExternalBitmapsStillHonorUnderuseAndInternalPressureRelease) {
  // A dense page followed by disjoint sparse pages must not pin its high-water
  // bitmap forever, even though the payload lives externally.
  std::vector<uint8_t> bytes(64, 0);
  memcpy(bytes.data(), "CPFONT\0\0", 8);
  put16(bytes, 8, CPFONT_VERSION);
  bytes[12] = 1;
  put32(bytes, 36, 2);
  put32(bytes, 40, 601);
  bytes[44] = 16;
  put16(bytes, 45, 12);
  put32(bytes, 56, 64);
  append(bytes, EpdUnicodeInterval{0x100, 0x100 + 599, 0});
  append(bytes, EpdUnicodeInterval{0xFFFD, 0xFFFD, 600});
  for (uint32_t i = 0; i < 601; ++i) {
    EpdGlyph g{};
    g.width = 8;
    g.height = 8;
    g.advanceX = 128;
    g.dataLength = 8;
    g.dataOffset = i * 8;
    append(bytes, g);
  }
  bytes.resize(bytes.size() + 601 * 8, 0xA5);
  Storage.put("large.cpfont", bytes);
  const auto range = [](int first, int count) {
    std::string result;
    for (int i = first; i < first + count; ++i) {
      const uint32_t cp = 0x100 + i;
      result.push_back(char(0xC0 | (cp >> 6)));
      result.push_back(char(0x80 | (cp & 63)));
    }
    return result;
  };
  SdCardFont font;
  ASSERT_TRUE(font.load("large.cpfont"));
  ASSERT_EQ(font.prewarm(range(0, 500).c_str(), 1), 0);
  ASSERT_NE(font.getEpdFont()->data->bitmap, nullptr);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(font.prewarm(range(500 + i * 25, 25).c_str(), 1), 0);
    font.clearCache();
    EXPECT_EQ(fakeheap::live.empty(), i == 2);
  }
  ASSERT_EQ(font.prewarm(range(0, 20).c_str(), 1), 0);
  fakeheap::internal.free = 39 * 1024;
  font.clearCache();
  EXPECT_TRUE(fakeheap::live.empty());
  EXPECT_EQ(font.getEpdFont()->data->bitmap, nullptr);
}

namespace {
// The fixture plus a kind-1 cluster table at the tail in the packed layout
// (Lipi table format 3): the format marker (a length-2 entry) and one
// length-3 entry (key ক্ষ -> U+E000, stored as the offset from U+E000).
// Blob: 18-byte directory, then per length the bucket index and entries.
// The last key byte of the entry sits at TABLE_LAST_KEY_BYTE (marker case).
constexpr size_t TABLE_LEN3_INDEX = Lipi::PACKED_DIRECTORY_BYTES + 3 + 4;  // after the length-2 section
constexpr size_t TABLE_LAST_KEY_BYTE = TABLE_LEN3_INDEX + 3 + 1;
std::vector<uint8_t> shapedFixture(const uint8_t lastKeyByte = 0xB7, const bool marker = true,
                                   const bool packedFlag = true) {
  std::vector<uint8_t> b = fixture();
  const uint32_t tableOffset = b.size();
  std::vector<uint8_t> t(Lipi::PACKED_DIRECTORY_BYTES, 0);
  if (marker) {
    t[0] = 1;  // length 2: one entry
    t[2] = 1;  // in one bucket
  }
  t[3] = 1;  // length 3: one entry
  t[5] = 1;  // in one bucket
  if (marker) {
    const uint8_t len2[7] = {0x06, 1, 0, 0x06, 2, Lipi::TABLE_FORMAT, 0x00};  // bucket 06; entry 06 | meta | value
    t.insert(t.end(), len2, len2 + 7);
  }
  const uint8_t len3[8] = {0x95, 1, 0, 0xCD, lastKeyByte, 3, 0x00, 0x00};  // bucket 95; entry CD B7 | meta | value
  t.insert(t.end(), len3, len3 + 8);
  b.insert(b.end(), t.begin(), t.end());
  b[33] = static_cast<uint8_t>(1 | (packedFlag ? Lipi::TOC_KIND_PACKED : 0));  // shapeKind (+ packed flag)
  put16(b, 34, marker ? 2 : 1);                                                // shapeEntryCount
  put32(b, 60, tableOffset);                                                   // shapeTableFileOffset
  return b;
}
bool registryEmpty() {
  for (const auto& e : ShapeTableCache::entries_) {
    if (e.data) return false;
  }
  return true;
}
}  // namespace

TEST_F(SdFontBitmapStorageTest, IdenticalClusterTablesShareOneCopy) {
  Storage.put("a.cpfont", shapedFixture());
  Storage.put("b.cpfont", shapedFixture());
  Storage.put("c.cpfont", shapedFixture(0xB8));  // same length and kind, different bytes
  {
    SdCardFont fa, fb, fc;
    ASSERT_TRUE(fa.load("a.cpfont"));
    ASSERT_TRUE(fb.load("b.cpfont"));
    ASSERT_TRUE(fc.load("c.cpfont"));
    const uint8_t* ta = fa.getEpdFont()->data->shapeTable;
    const uint8_t* tb = fb.getEpdFont()->data->shapeTable;
    const uint8_t* tc = fc.getEpdFont()->data->shapeTable;
    ASSERT_NE(ta, nullptr);
    EXPECT_EQ(ta, tb);
    EXPECT_NE(ta, tc);
    EXPECT_EQ(ShapeTableCache::refs(ta), 2);
    EXPECT_EQ(ShapeTableCache::refs(tc), 1);
    EXPECT_EQ(fa.getEpdFont()->data->shapeEntryCount, 2);
    EXPECT_EQ(fa.getEpdFont()->data->shapeKind, 1);
    EXPECT_EQ(ta[TABLE_LAST_KEY_BYTE], 0xB7);  // the length-2 marker section precedes the entry
    EXPECT_EQ(tc[TABLE_LAST_KEY_BYTE], 0xB8);
    // Reloading a font without a table drops its reference; the other keeps the bytes.
    ASSERT_TRUE(fa.load("font.cpfont"));
    EXPECT_EQ(fa.getEpdFont()->data->shapeTable, nullptr);
    EXPECT_EQ(ShapeTableCache::refs(tb), 1);
    EXPECT_EQ(tb[TABLE_LEN3_INDEX], 0x95);  // the length-3 bucket, after the marker section
  }
  EXPECT_TRUE(registryEmpty());
}

TEST_F(SdFontBitmapStorageTest, ClusterTableWithoutFormatMarkerIsIgnored) {
  Storage.put("old.cpfont", shapedFixture(0xB7, false));
  SdCardFont font;
  ASSERT_TRUE(font.load("old.cpfont"));
  EXPECT_EQ(font.getEpdFont()->data->shapeTable, nullptr);
  EXPECT_EQ(font.getEpdFont()->data->shapeEntryCount, 0);
  EXPECT_EQ(font.getEpdFont()->data->shapeKind, 0);
  EXPECT_TRUE(registryEmpty());
}

TEST_F(SdFontBitmapStorageTest, ClusterTableInTheFlatLayoutIsIgnored) {
  // A kind byte without the packed flag is the converter before table
  // format 3: the table is ignored, the glyphs stay usable.
  Storage.put("flat.cpfont", shapedFixture(0xB7, true, false));
  SdCardFont font;
  ASSERT_TRUE(font.load("flat.cpfont"));
  EXPECT_EQ(font.getEpdFont()->data->shapeTable, nullptr);
  EXPECT_EQ(font.getEpdFont()->data->shapeEntryCount, 0);
  EXPECT_EQ(font.getEpdFont()->data->shapeKind, 0);
  EXPECT_TRUE(registryEmpty());
}

TEST_F(SdFontBitmapStorageTest, ClusterTableRegistryOverflowStillFrees) {
  ASSERT_TRUE(registryEmpty());
  const uint8_t* tables[ShapeTableCache::MAX_ENTRIES + 2];
  for (uint32_t i = 0; i < ShapeTableCache::MAX_ENTRIES + 2; i++) {
    auto* bytes = new uint8_t[16];
    memset(bytes, 0, 16);
    bytes[0] = static_cast<uint8_t>(i);
    bool shared = true;
    tables[i] = ShapeTableCache::acquire(bytes, 16, 2, &shared);
    EXPECT_FALSE(shared);
    EXPECT_EQ(tables[i], bytes);
  }
  EXPECT_EQ(ShapeTableCache::refs(tables[0]), 1);
  EXPECT_EQ(ShapeTableCache::refs(tables[ShapeTableCache::MAX_ENTRIES]), 0);  // unregistered, still owned
  // A duplicate of an unregistered table is not shared; a duplicate of a registered one is.
  auto* dup = new uint8_t[16];
  memset(dup, 0, 16);
  bool shared = false;
  const uint8_t* got = ShapeTableCache::acquire(dup, 16, 2, &shared);
  EXPECT_TRUE(shared);
  EXPECT_EQ(got, tables[0]);
  EXPECT_EQ(ShapeTableCache::refs(tables[0]), 2);
  ShapeTableCache::release(got);
  for (const uint8_t* t : tables) ShapeTableCache::release(t);
  EXPECT_TRUE(registryEmpty());
}
