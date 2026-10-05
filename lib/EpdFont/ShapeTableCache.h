#pragma once

#include <cstdint>
#include <cstring>

/// Process-wide registry of resident script cluster tables (EpdFontData::shapeTable).
///
/// Every SdCardFont instance of a family (one per point size, plus the UI
/// fallback sizes) loads its own copy of the table, and those copies are
/// normally byte-identical: the builder derives the table from the font's
/// OpenType data, not from the size. Sharing is by content, so one copy stays
/// resident per distinct table and a size whose table does differ simply
/// keeps its own. Entries are matched by length, kind, hash and a full
/// compare, so two tables that merely hash alike never alias.
///
/// Ownership: acquire() takes the caller's buffer (allocated with new[]) and
/// returns the resident pointer; when an identical table is already cached
/// the caller's buffer is freed. release() drops one reference and frees the
/// bytes at zero. A pointer the registry does not know (the registry was full
/// when it was acquired) is simply freed. Nothing outlives its last font.
///
/// Header-only so the host tests that compile SdCardFont.cpp need no extra
/// source. Not thread-safe: fonts are loaded and freed from one task.
namespace ShapeTableCache {

constexpr uint8_t MAX_ENTRIES = 8;

struct Entry {
  const uint8_t* data = nullptr;
  uint32_t bytes = 0;
  uint32_t hash = 0;
  uint8_t kind = 0;
  uint8_t refs = 0;
};

inline Entry entries_[MAX_ENTRIES]{};

inline uint32_t hashOf(const uint8_t* p, const uint32_t n, const uint8_t kind) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 16777619u;
  }
  h ^= kind;
  h *= 16777619u;
  return h;
}

inline Entry* find(const uint8_t* table) {
  if (!table) return nullptr;
  for (auto& e : entries_) {
    if (e.data == table) return &e;
  }
  return nullptr;
}

/// Registers `bytes` (new[]-allocated, `len` long, table kind `kind`) and
/// returns the resident table. `shared` is set when an identical table was
/// already resident, in which case `bytes` has been freed.
inline const uint8_t* acquire(uint8_t* bytes, const uint32_t len, const uint8_t kind, bool* shared = nullptr) {
  if (shared) *shared = false;
  if (!bytes || len == 0) return bytes;
  const uint32_t h = hashOf(bytes, len, kind);
  Entry* freeSlot = nullptr;
  for (auto& e : entries_) {
    if (e.data == nullptr) {
      if (!freeSlot) freeSlot = &e;
      continue;
    }
    if (e.bytes == len && e.kind == kind && e.hash == h && e.refs < 255 && memcmp(e.data, bytes, len) == 0) {
      delete[] bytes;
      e.refs++;
      if (shared) *shared = true;
      return e.data;
    }
  }
  if (freeSlot) *freeSlot = Entry{bytes, len, h, kind, 1};
  return bytes;
}

/// Drops one reference to `table`; frees it at zero or when it is not registered.
inline void release(const uint8_t* table) {
  if (!table) return;
  if (Entry* e = find(table)) {
    if (--e->refs > 0) return;
    *e = Entry{};
  }
  delete[] table;
}

/// Number of fonts sharing `table` (0 when unknown to the registry).
inline uint8_t refs(const uint8_t* table) {
  const Entry* e = find(table);
  return e ? e->refs : 0;
}

}  // namespace ShapeTableCache
