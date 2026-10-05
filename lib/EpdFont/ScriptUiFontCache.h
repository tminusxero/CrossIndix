#pragma once

#include <cstdint>
#include <cstring>

// Bookkeeping for the SD-card font families kept resident for UI strings in
// scripts the built-in fonts lack (see SdCardFontSystem). A family is the
// unit of residency because one file can cover several scripts (a CJK font
// covers Han, kana and Hangul). Each resident family holds up to kSizes font
// ids, one per built-in UI point size, loaded lazily. The owner evicts by free
// heap: leastRecentlyUsedSize() names the loaded size nobody drew for the
// longest time (unused sizes go before whole families). When all slots are
// taken the least recently used family is replaced. Pure bookkeeping: loading
// and unloading are done by the owner, so this stays host-testable.
class ScriptUiFontCache {
 public:
  static constexpr uint8_t kSizes = 3;
  static constexpr uint8_t kMaxEntries = 8;
  static constexpr uint8_t kFamilyNameLen = 64;

  struct Entry {
    char family[kFamilyNameLen] = {};
    int fontIds[kSizes] = {0, 0, 0};
    uint32_t sizeLastUse[kSizes] = {0, 0, 0};
    uint32_t lastUse = 0;
    bool used = false;
  };

  explicit ScriptUiFontCache(const uint8_t cap = 1) : cap_(cap > kMaxEntries ? kMaxEntries : (cap == 0 ? 1 : cap)) {}

  uint8_t capacity() const { return cap_; }

  // Resident entry for `family`, marked as most recently used. nullptr if absent.
  Entry* touch(const char* family) {
    Entry* e = find(family);
    if (e) e->lastUse = ++clock_;
    return e;
  }

  const Entry* peek(const char* family) const { return find(family); }
  // Mark size `sizeIndex` of `entry` as just drawn.
  void touchSize(Entry& entry, const int sizeIndex) {
    if (sizeIndex < 0 || sizeIndex >= kSizes) return;
    entry.lastUse = ++clock_;
    entry.sizeLastUse[sizeIndex] = entry.lastUse;
  }
  struct Victim {
    char family[kFamilyNameLen] = {};
    int sizeIndex = -1;
    int fontId = 0;
  };
  // The loaded size drawn least recently, other than `keepFamily` at
  // `keepSize` (the one about to be used). False when nothing else is loaded.
  bool leastRecentlyUsedSize(const char* keepFamily, const int keepSize, Victim& out) const {
    const Entry* best = nullptr;
    int bestSize = -1;
    for (uint8_t i = 0; i < cap_; ++i) {
      const Entry& e = entries_[i];
      if (!e.used) continue;
      const bool isKeep = keepFamily && std::strncmp(e.family, keepFamily, kFamilyNameLen) == 0;
      for (int z = 0; z < kSizes; ++z) {
        if (e.fontIds[z] == 0 || (isKeep && z == keepSize)) continue;
        if (!best || e.sizeLastUse[z] < best->sizeLastUse[bestSize]) {
          best = &e;
          bestSize = z;
        }
      }
    }
    if (!best) return false;
    out = Victim{};
    std::strncpy(out.family, best->family, kFamilyNameLen - 1);
    out.sizeIndex = bestSize;
    out.fontId = best->fontIds[bestSize];
    return true;
  }
  // Forget one size of `family` (owner has unloaded it); the family goes when
  // it has no size left.
  void clearSize(const char* family, const int sizeIndex) {
    Entry* e = find(family);
    if (!e || sizeIndex < 0 || sizeIndex >= kSizes) return;
    e->fontIds[sizeIndex] = 0;
    e->sizeLastUse[sizeIndex] = 0;
    for (int z = 0; z < kSizes; ++z) {
      if (e->fontIds[z] != 0) return;
    }
    *e = Entry{};
  }

  // Entry to use for `family`, creating it when absent. When creating and the
  // cache is full, the least recently used entry is copied to `evicted` (so
  // the owner can unload its fonts) and reused. `evicted.used` is false when
  // nothing had to go.
  Entry& acquire(const char* family, Entry& evicted) {
    evicted = Entry{};
    if (Entry* e = touch(family)) return *e;
    Entry* slot = nullptr;
    for (uint8_t i = 0; i < cap_; ++i) {
      if (!entries_[i].used) {
        slot = &entries_[i];
        break;
      }
    }
    if (!slot) {
      slot = &entries_[0];
      for (uint8_t i = 1; i < cap_; ++i) {
        if (entries_[i].lastUse < slot->lastUse) slot = &entries_[i];
      }
      evicted = *slot;
    }
    *slot = Entry{};
    std::strncpy(slot->family, family, kFamilyNameLen - 1);
    slot->used = true;
    slot->lastUse = ++clock_;
    return *slot;
  }

  // Forget `family` (owner has unloaded it). Returns the dropped entry.
  Entry release(const char* family) {
    Entry out{};
    if (Entry* e = find(family)) {
      out = *e;
      *e = Entry{};
    }
    return out;
  }

  // Drop everything; the owner unloads the fonts listed in `out` (up to
  // kMaxEntries). Returns how many entries were resident.
  uint8_t releaseAll(Entry* out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < cap_; ++i) {
      if (!entries_[i].used) continue;
      if (out) out[n] = entries_[i];
      entries_[i] = Entry{};
      ++n;
    }
    return n;
  }

  // Drop every entry except `keep` (may be null or empty: drop all); the owner
  // unloads the fonts listed in `out` (up to kMaxEntries). Returns how many
  // entries were dropped.
  uint8_t releaseAllExcept(const char* keep, Entry* out) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < cap_; ++i) {
      if (!entries_[i].used) continue;
      if (keep && keep[0] != '\0' && std::strncmp(entries_[i].family, keep, kFamilyNameLen) == 0) continue;
      if (out) out[n] = entries_[i];
      entries_[i] = Entry{};
      ++n;
    }
    return n;
  }
  uint8_t residentCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < cap_; ++i) n += entries_[i].used ? 1 : 0;
    return n;
  }

 private:
  Entry* find(const char* family) {
    if (!family || family[0] == '\0') return nullptr;
    for (uint8_t i = 0; i < cap_; ++i) {
      if (entries_[i].used && std::strncmp(entries_[i].family, family, kFamilyNameLen) == 0) return &entries_[i];
    }
    return nullptr;
  }
  const Entry* find(const char* family) const { return const_cast<ScriptUiFontCache*>(this)->find(family); }

  Entry entries_[kMaxEntries];
  uint8_t cap_;
  uint32_t clock_ = 0;
};
