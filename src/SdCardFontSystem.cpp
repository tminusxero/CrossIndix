#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MemoryBudget.h>
#include <SdCardFont.h>
#include <Utf8.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};

// Built-in UI font ids and their point sizes. Script fallback fonts are
// loaded at exactly these sizes so foreign-script strings sit at the same
// size as the Latin UI text around them. Reader sizes are a separate matter.
constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};
constexpr uint8_t kUiFontSizeCount = static_cast<uint8_t>(std::size(kUiFontSizes));
static_assert(kUiFontSizeCount == ScriptUiFontCache::kSizes, "cache slots must match UI sizes");

int uiSizeIndexFor(const int primaryFontId) {
  for (uint8_t i = 0; i < kUiFontSizeCount; i++) {
    if (kUiFontSizes[i].fontId == primaryFontId) return i;
  }
  return -1;
}

// First script block in `text` that the built-in UI fonts lack, ignoring the
// shared Indic dandas. None when the string is Latin/Cyrillic/... only.
ScriptBlock firstFallbackScript(const char* text) {
  if (!text) return ScriptBlock::None;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&p))) {
    if (isIndicDanda(cp)) continue;
    const ScriptBlock b = scriptBlockOf(cp);
    if (b != ScriptBlock::None) return b;
  }
  return ScriptBlock::None;
}

enum class FontFileSelection : uint8_t { Closest, Exact };

// This is a cold setup path, not a render loop. The 320-byte stack footprint
// (path and filename) replace a heap-allocated whole-font catalog
// during every dictionary swap, avoiding persistent fragmentation on the C3.
bool findInstalledFontFile(const char* familyName, const uint8_t targetPointSize, const FontFileSelection selection,
                           char* path, const size_t pathSize, uint8_t& selectedPointSize) {
  if (!familyName || familyName[0] == '\0' || !path || pathSize == 0) return false;

  const char* root = SdCardFontRegistry::findFamilyRoot(familyName);
  if (!root) return false;
  const int directoryLength = std::snprintf(path, pathSize, "%s/%s", root, familyName);
  if (directoryLength <= 0 || static_cast<size_t>(directoryLength) >= pathSize) return false;

  HalFile dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) return false;

  uint8_t closestSize = 0;
  uint8_t closestDiff = UINT8_MAX;
  char filename[128] = {};
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    const bool isDirectory = entry.isDirectory();
    if (!isDirectory) entry.getName(filename, sizeof(filename));
    entry.close();
    if (isDirectory) continue;

    uint8_t pointSize = 0;
    uint8_t style = 0;
    if (!SdCardFontRegistry::parseFilename(filename, pointSize, style) || style != 0) continue;

    if (selection == FontFileSelection::Closest) {
      const uint8_t diff = pointSize > targetPointSize ? pointSize - targetPointSize : targetPointSize - pointSize;
      if (closestDiff == UINT8_MAX || diff < closestDiff || (diff == closestDiff && pointSize < closestSize)) {
        closestSize = pointSize;
        closestDiff = diff;
      }
    }
  }
  dir.close();

  if (selection == FontFileSelection::Closest) {
    selectedPointSize = closestSize;
  } else if (selection == FontFileSelection::Exact) {
    selectedPointSize = targetPointSize;
  }

  if (selectedPointSize == 0) return false;
  // Scan once more to preserve the exact file name rather than assuming the
  // file base name matches the directory name.
  dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) return false;
  while (true) {
    HalFile entry = dir.openNextFile();
    if (!entry) break;
    const bool isDirectory = entry.isDirectory();
    if (!isDirectory) entry.getName(filename, sizeof(filename));
    entry.close();
    if (isDirectory) continue;

    uint8_t pointSize = 0;
    uint8_t style = 0;
    if (!SdCardFontRegistry::parseFilename(filename, pointSize, style) || style != 0 ||
        pointSize != selectedPointSize) {
      continue;
    }
    const int pathLength = std::snprintf(path, pathSize, "%s/%s/%s", root, familyName, filename);
    dir.close();
    return pathLength > 0 && static_cast<size_t>(pathLength) < pathSize;
  }
  dir.close();
  return false;
}

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t pointSize) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, pointSize);
  };
  SETTINGS.sdFontResolverCtx = this;
  renderer_ = &renderer;

  int primaries[kUiFontSizeCount];
  for (uint8_t i = 0; i < kUiFontSizeCount; i++) primaries[i] = kUiFontSizes[i].fontId;
  renderer.setFallbackResolver(&SdCardFontSystem::resolveUiFontTrampoline, this, primaries, kUiFontSizeCount);

  // Index which installed family covers which script before the first UI
  // string is drawn, whether or not a reader family is selected: file names
  // and titles in the browser must not depend on the reader font.
  ensureRegistry();
  if (SETTINGS.sdFontFamilyName[0] != '\0') ensureLoaded(renderer);
  releaseRegistry();
}

void SdCardFontSystem::persistSettingsChange() const {
  if (settingsPersistenceCallback_) {
    settingsPersistenceCallback_(settingsPersistenceContext_);
  } else {
    SETTINGS.saveToFile();
  }
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  bool registryWasDirty = registryDirty_.load(std::memory_order_acquire) || fontReloadPending_ ||
                          loadedRegistryRevision_ != registry_.revision();
  uiFallbackSuspended_ = false;

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();
  uint8_t targetPointSize = SETTINGS.getSdFontTargetPointSize();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
      loadedFontPointSize_ = 0;
    }
    return;
  }

  if (!registryWasDirty && currentFamily == wantedFamily && loadedFontPointSize_ == targetPointSize &&
      SETTINGS.legacySdFontSizeStep == UINT8_MAX) {
    return;
  }

  ensureRegistry();
  if (registry_.lastDiscoveryFailed()) return;
  registryWasDirty = registryWasDirty || fontReloadPending_ || loadedRegistryRevision_ != registry_.revision();

  const auto* family = registry_.findFamily(wantedFamily);
  if (family && !family->ensureDetails()) return;
  if (family && SETTINGS.legacySdFontSizeStep != UINT8_MAX) {
    const auto sizes = family->availableSizes();
    if (!sizes.empty()) {
      const uint8_t step = std::min<uint8_t>(SETTINGS.legacySdFontSizeStep, sizes.size() - 1);
      targetPointSize = sizes[step];
      SETTINGS.readerFontPointSize = targetPointSize;
      SETTINGS.legacySdFontSizeStep = UINT8_MAX;
      persistSettingsChange();
      LOG_INF("SDFS", "Migrated SD font size to %u pt", targetPointSize);
    }
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.sdFontFamilyName[0] = '\0';
      if (!transientFontLoad_) persistSettingsChange();
      return;
    }
    const auto* wantedFile = family->findClosestFile(targetPointSize);
    uint8_t wantedPt = wantedFile ? wantedFile->pointSize : 0;
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u (target %u)%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            targetPointSize, registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  if (family) {
    if (manager_.loadFamilyClosest(*family, renderer, targetPointSize)) {
      loadedFontPointSize_ = targetPointSize;
      fontReloadPending_ = false;
      loadedRegistryRevision_ = registry_.revision();
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.sdFontFamilyName[0] = '\0';
      if (!transientFontLoad_) persistSettingsChange();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.sdFontFamilyName[0] = '\0';
    if (!transientFontLoad_) persistSettingsChange();
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) {
  // UI script fonts are dropped too; they come back lazily on the next
  // foreign-script string (guarded by the heap check in uiFontFor).
  dropUiFonts(renderer);
  if (manager_.currentFamilyName().empty()) return;

  const std::string familyName = manager_.currentFamilyName();
  manager_.unloadAll(renderer);
  loadedFontPointSize_ = 0;
  LOG_DBG("SDFS", "Released SD card font before low-memory operation: %s", familyName.c_str());
}

void SdCardFontSystem::dropUiFonts(GfxRenderer& renderer) {
  ScriptUiFontCache::Entry dropped[ScriptUiFontCache::kMaxEntries];
  const uint8_t n = uiCache_.releaseAll(dropped);
  for (uint8_t i = 0; i < n; i++) manager_.unloadUiFamily(dropped[i].family, renderer);
  manager_.unloadAllUiFonts(renderer);
}

void SdCardFontSystem::dropUiFamiliesForBook(GfxRenderer& renderer) {
  ScriptUiFontCache::Entry dropped[ScriptUiFontCache::kMaxEntries];
  const uint8_t n = uiCache_.releaseAllExcept(manager_.currentFamilyName().c_str(), dropped);
  for (uint8_t i = 0; i < n; i++) {
    LOG_DBG("SDFS", "Book open: unloading UI script family %s", dropped[i].family);
    manager_.unloadUiFamily(dropped[i].family, renderer);
  }
}

void SdCardFontSystem::suspendUiFallbacks(GfxRenderer& renderer) {
  dropUiFonts(renderer);
  uiFallbackSuspended_ = true;
}

void SdCardFontSystem::ensureRegistry() {
  const bool dirty = registryDirty_.exchange(false, std::memory_order_acq_rel);
  if (dirty) fontReloadPending_ = true;
  if (registryLoaded_ && !dirty && !registry_.needsRefresh()) return;
  if (dirty) LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
  registry_.loadNames();
  if (registry_.lastDiscoveryFailed()) {
    LOG_ERR("SDFS", "SD font registry scan ran out of memory (free=%u maxAlloc=%u)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    registryDirty_.store(true, std::memory_order_release);
    return;
  }
  registryLoaded_ = true;
  // The script index survives releaseRegistry(); rebuild it only the first
  // time and when files were added, replaced or removed, in which case the
  // resident UI fonts could point at stale content and are dropped too.
  if (scriptIndexBuilt_ && !dirty) return;
  if (dirty && renderer_) dropUiFonts(*renderer_);
  rebuildScriptIndex();
  scriptIndexBuilt_ = true;
}

void SdCardFontSystem::rebuildScriptIndex() {
  std::memset(scriptFamilies_, 0, sizeof(scriptFamilies_));
  std::memset(scriptUiSizes_, 0, sizeof(scriptUiSizes_));

  // Probe every script letter except None in one pass per family.
  constexpr uint8_t probeCount = kScriptCount - 1;
  uint32_t probes[probeCount];
  for (uint8_t i = 0; i < probeCount; i++) probes[i] = kScriptProbe[i + 1];

  for (const auto& family : registry_.getFamilies()) {
    uint8_t sizesMask = 0;
    std::string probePath;
    for (uint8_t i = 0; i < kUiFontSizeCount; i++) {
      const auto* file = family.findFile(kUiFontSizes[i].pointSize);  // loads the family's file list
      if (!file) continue;
      sizesMask |= static_cast<uint8_t>(1u << i);
      probePath = file->path;  // largest installed UI size
    }
    // The file list is only needed here; the registry stays names-only until
    // a family is loaded (ensureLoaded re-reads the details it needs).
    family.releaseDetails();
    if (sizesMask == 0) continue;

    const uint32_t hits = SdCardFont::probeCoverage(probePath.c_str(), probes, probeCount);
    if (hits == 0) continue;
    const bool isReaderFamily = family.name == SETTINGS.sdFontFamilyName;
    for (uint8_t i = 0; i < probeCount; i++) {
      if (!(hits & (1u << i))) continue;
      const uint8_t block = i + 1;
      if (scriptFamilies_[block][0] != '\0' && !isReaderFamily) continue;
      std::strncpy(scriptFamilies_[block], family.name.c_str(), ScriptUiFontCache::kFamilyNameLen - 1);
      scriptUiSizes_[block] = sizesMask;
    }
  }
  for (uint8_t b = 1; b < kScriptCount; b++) {
    if (scriptFamilies_[b][0] != '\0') {
      LOG_DBG("SDFS", "UI fallback script %u -> %s (sizes 0x%x)", b, scriptFamilies_[b], scriptUiSizes_[b]);
    }
  }
}

const char* SdCardFontSystem::familyForScript(const ScriptBlock block) const {
  const uint8_t b = static_cast<uint8_t>(block);
  return b < kScriptCount ? scriptFamilies_[b] : "";
}

int SdCardFontSystem::resolveUiFontTrampoline(void* ctx, const uint8_t block, const int primaryFontId) {
  auto* self = static_cast<SdCardFontSystem*>(ctx);
  if (!self || !self->renderer_ || block >= kScriptCount) return 0;
  return self->uiFontFor(static_cast<ScriptBlock>(block), primaryFontId, *self->renderer_);
}

int SdCardFontSystem::uiFontFor(const ScriptBlock block, const int primaryFontId, GfxRenderer& renderer) {
  if (uiFallbackSuspended_) return 0;
  const uint8_t b = static_cast<uint8_t>(block);
  if (b == 0 || b >= kScriptCount) return 0;
  const int sizeIndex = uiSizeIndexFor(primaryFontId);
  if (sizeIndex < 0) return 0;
  const char* family = scriptFamilies_[b];
  if (family[0] == '\0' || !(scriptUiSizes_[b] & (1u << sizeIndex))) return 0;
  const uint8_t pointSize = kUiFontSizes[sizeIndex].pointSize;

  // The reader family loaded at this exact size serves as-is.
  if (const int readerId = manager_.readerFontIdAt(family, pointSize)) return readerId;

  ScriptUiFontCache::Entry* entry = uiCache_.touch(family);
  if (entry) {
    const int id = entry->fontIds[sizeIndex];
    if (id != 0 && renderer.getFontMap().count(id) != 0) {
      uiCache_.touchSize(*entry, sizeIndex);
      return id;
    }
    entry->fontIds[sizeIndex] = 0;  // unloaded elsewhere (reader adopted then released)
  }

  // Make room by heap, not by a family count: unload the least recently drawn
  // UI font sizes (other than this one) while free heap is short.
  auto heap = MemoryBudget::snapshot();
  ScriptUiFontCache::Victim victim;
  while ((heap.freeHeap < MemoryBudget::UI_SD_FONT_EVICT_BELOW_FREE ||
          heap.maxAllocHeap < MemoryBudget::UI_SD_FONT_MIN_MAX_ALLOC) &&
         uiCache_.leastRecentlyUsedSize(family, sizeIndex, victim)) {
    LOG_DBG("SDFS", "Low heap (free=%u): unloading UI font %s size %d", heap.freeHeap, victim.family, victim.sizeIndex);
    manager_.unloadUiFont(victim.fontId, renderer);
    uiCache_.clearSize(victim.family, victim.sizeIndex);
    heap = MemoryBudget::snapshot();
  }
  entry = uiCache_.touch(family);  // clearSize may have dropped this family's entry

  if (heap.freeHeap < MemoryBudget::UI_SD_FONT_MIN_FREE || heap.maxAllocHeap < MemoryBudget::UI_SD_FONT_MIN_MAX_ALLOC) {
    if (!uiHeapWarned_) {
      LOG_DBG("SDFS", "Low heap for UI font %s %u pt (free=%u maxAlloc=%u); leaving replacement glyphs", family,
              pointSize, heap.freeHeap, heap.maxAllocHeap);
      uiHeapWarned_ = true;
    }
    return 0;
  }
  uiHeapWarned_ = false;

  char path[160] = {};
  uint8_t selected = 0;
  if (!findInstalledFontFile(family, pointSize, FontFileSelection::Exact, path, sizeof(path), selected)) {
    LOG_DBG("SDFS", "UI font file missing: %s %u pt", family, pointSize);
    scriptUiSizes_[b] &= static_cast<uint8_t>(~(1u << sizeIndex));
    return 0;
  }

  if (!entry) {
    ScriptUiFontCache::Entry evicted;
    entry = &uiCache_.acquire(family, evicted);
    if (evicted.used) {
      LOG_DBG("SDFS", "Evicting UI script family %s", evicted.family);
      manager_.unloadUiFamily(evicted.family, renderer);
    }
  }
  const int id = manager_.loadUiFont(path, family, pointSize, renderer);
  entry->fontIds[sizeIndex] = id;
  if (id != 0) uiCache_.touchSize(*entry, sizeIndex);
  if (id != 0) LOG_DBG("SDFS", "UI script font ready: %s %u pt (script %u)", family, pointSize, b);
  return id;
}

bool SdCardFontSystem::ensureReaderFontCovers(GfxRenderer& renderer, const char* text) {
  const ScriptBlock block = firstFallbackScript(text);
  if (block == ScriptBlock::None) return true;
  const uint32_t probe = scriptProbe(block);

  const auto readerIt = renderer.getFontMap().find(SETTINGS.getReaderFontId());
  if (readerIt != renderer.getFontMap().end() && readerIt->second.hasCodepoint(probe)) return true;

  const char* family = familyForScript(block);
  if (family[0] == '\0') {
    LOG_DBG("SDFS", "No installed family covers script %u of the book title", static_cast<uint8_t>(block));
    return false;
  }
  if (std::strcmp(SETTINGS.sdFontFamilyName, family) == 0) return false;  // selected but not loadable

  LOG_INF("SDFS", "Reader font lacks script %u; using %s for this book", static_cast<uint8_t>(block), family);
  char previous[sizeof(SETTINGS.sdFontFamilyName)];
  std::memcpy(previous, SETTINGS.sdFontFamilyName, sizeof(previous));
  std::strncpy(SETTINGS.sdFontFamilyName, family, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  transientFontLoad_ = true;
  ensureLoaded(renderer);
  transientFontLoad_ = false;
  if (manager_.currentFamilyName() == family) return true;
  // The load failed and ensureLoaded cleared the name: that was this book's
  // try, not the user's setting, so the chosen family comes back (it is
  // reloaded on the next ensureLoaded).
  std::memcpy(SETTINGS.sdFontFamilyName, previous, sizeof(previous));
  LOG_ERR("SDFS", "Could not load %s for the book title; keeping %s", family,
          previous[0] ? previous : "the built-in font");
  return false;
}

void SdCardFontSystem::releaseRegistry() {
  if (!registryLoaded_) return;
  LOG_DBG("SDFS", "Releasing SD font catalog (%d families)", registry_.getFamilyCount());
  registry_.clear();
  registryLoaded_ = false;
}

void SdCardFontSystem::releaseForNetwork(GfxRenderer& renderer) {
  suspendUiFallbacks(renderer);
  releaseLoadedFont(renderer);

  releaseRegistry();
  registryDirty_.store(true, std::memory_order_release);
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
  // The manager loads exactly one size (closest to the selected point size), so the
  // enum is implicit — always return the single loaded font ID for this family.
  // ensureLoaded() must have been called with the current settings before this.
  return manager_.getFontId(familyName);
}

bool SdCardFontSystem::changeReaderFontSize(const bool larger, const FontSizeStepMode mode) {
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    refreshIfDirty();
    if (registry_.lastDiscoveryFailed()) return false;
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      if (!family->ensureDetails()) return false;
      const auto sizes = family->availableSizes();
      if (changeReaderFontSizeStep(sizes.data(), sizes.size(), SETTINGS.readerFontPointSize, larger, mode)) return true;
      if (sizes.size() > 0) return false;
    }
  }

  return SETTINGS.changeReaderFontSize(larger, mode);
}

uint8_t SdCardFontSystem::resolveLegacySizeStep(const char* familyName, const uint8_t sizeStep) {
  ensureRegistry();
  const auto* family = familyName ? registry_.findFamily(familyName) : nullptr;
  if (family) {
    const auto sizes = family->availableSizes();
    if (!sizes.empty()) return sizes[std::min<uint8_t>(sizeStep, sizes.size() - 1)];
  }
  return CrossPointSettings::getSdFontRangePointSize(SETTINGS.sdFontSizeRange, sizeStep);
}

DictionaryFontActivation SdCardFontSystem::activateDictionaryFont(GfxRenderer& renderer, const char* familyName,
                                                                  uint8_t targetPointSize) {
  // A non-zero size with no dedicated family means "use the reader's installed
  // family at this size". This keeps the setting useful when the same custom
  // family is wanted for reading and definitions without keeping two families
  // resident.
  if ((!familyName || familyName[0] == '\0') && targetPointSize != 0 && SETTINGS.sdFontFamilyName[0] != '\0') {
    familyName = SETTINGS.sdFontFamilyName;
  }
  if (!familyName || familyName[0] == '\0') {
    return {restoreReaderFont(renderer), false};
  }

  MemoryBudget::logHeapShape("dict.font_before_activate");
  char path[160] = {};
  uint8_t selectedPointSize = 0;
  // Prefer the actual loaded reader-file size. Built-in readers have no SD
  // file, so use their effective physical point size instead.
  if (targetPointSize == 0) {
    targetPointSize = manager_.currentPointSize() != 0
                          ? manager_.currentPointSize()
                          : CrossPointSettings::getReaderFontPointSize(SETTINGS.getEffectiveReaderFontSize());
  }
  if (!findInstalledFontFile(familyName, targetPointSize, FontFileSelection::Closest, path, sizeof(path),
                             selectedPointSize)) {
    LOG_DBG("SDFS", "Dictionary font not found on card: %s", familyName);
    const char* globalFamilyName = SETTINGS.dictionarySdFontFamilyName;
    if (globalFamilyName[0] != '\0' && std::strcmp(familyName, globalFamilyName) != 0) {
      LOG_DBG("SDFS", "Using global dictionary font while per-book font is unavailable: %s", globalFamilyName);
      return activateDictionaryFont(renderer, globalFamilyName, SETTINGS.dictionaryFontPointSize);
    }
    const int readerFontId = restoreReaderFont(renderer);
    MemoryBudget::logHeapShape("dict.font_reader_fallback");
    return {readerFontId, false};
  }

  if (manager_.currentFamilyName() == familyName && manager_.currentPointSize() == selectedPointSize) {
    const int fontId = manager_.getFontId(manager_.currentFamilyName());
    MemoryBudget::logHeapShape("dict.font_reused_reader");
    return {fontId, true};
  }

  // A reader SD font can retain page glyphs, kerning, and advance tables after
  // a long reading session. They are disposable at this handoff: keeping them
  // through the headroom check makes a dictionary font appear unavailable until
  // its book cache is deleted or the heap happens to be less fragmented.
  const int activeReaderFontId = SETTINGS.getReaderFontId();
  const auto beforeCacheRelease = MemoryBudget::snapshot();
  if (renderer.releaseSdCardFontForLowMemory(activeReaderFontId)) {
    const auto afterCacheRelease = MemoryBudget::snapshot();
    LOG_DBG("SDFS", "Released reader SD-font caches before dictionary swap: free=%u->%u maxAlloc=%u->%u",
            beforeCacheRelease.freeHeap, afterCacheRelease.freeHeap, beforeCacheRelease.maxAllocHeap,
            afterCacheRelease.maxAllocHeap);
  }

  auto heap = MemoryBudget::snapshot();
  if (!MemoryBudget::hasHeapForDictionarySdFont(heap)) {
    // The reader family itself is also disposable for a dictionary swap. Retry
    // after releasing it so its font data does not cause a false low-memory
    // fallback.
    const auto beforeReaderUnload = heap;
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    loadedFontPointSize_ = 0;
    heap = MemoryBudget::snapshot();
    LOG_DBG("SDFS", "Released reader font before dictionary swap retry: free=%u->%u maxAlloc=%u->%u",
            beforeReaderUnload.freeHeap, heap.freeHeap, beforeReaderUnload.maxAllocHeap, heap.maxAllocHeap);
  }
  if (!MemoryBudget::hasHeapForDictionarySdFont(heap)) {
    LOG_ERR("SDFS", "Low heap for dictionary font swap (%u free, %u max alloc, need %u/%u); using reader font",
            heap.freeHeap, heap.maxAllocHeap, MemoryBudget::DICTIONARY_SD_FONT_MIN_FREE,
            MemoryBudget::DICTIONARY_SD_FONT_MIN_MAX_ALLOC);
    const int readerFontId = restoreReaderFont(renderer);
    MemoryBudget::logHeapShape("dict.font_heap_fallback");
    return {readerFontId, false};
  }

  // Drop the reader family before the dictionary file is allocated, so both
  // reader families are never resident at once.
  if (!manager_.currentFamilyName().empty()) {
    manager_.unloadAll(renderer);
  }
  loadedFontPointSize_ = 0;

  if (manager_.loadFamilyFile(path, familyName, selectedPointSize, renderer)) {
    const int fontId = manager_.getFontId(manager_.currentFamilyName());
    LOG_DBG("SDFS", "Activated dictionary font %s at %u pt", familyName, manager_.currentPointSize());
    MemoryBudget::logHeapShape("dict.font_after_activate");
    return {fontId, true};
  }

  LOG_ERR("SDFS", "Failed to load dictionary font %s; restoring reader font", familyName);
  const int readerFontId = restoreReaderFont(renderer);
  MemoryBudget::logHeapShape("dict.font_reader_fallback");
  return {readerFontId, false};
}

int SdCardFontSystem::restoreReaderFont(GfxRenderer& renderer) {
  const char* familyName = SETTINGS.sdFontFamilyName;
  if (!familyName || familyName[0] == '\0') {
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    loadedFontPointSize_ = 0;
    MemoryBudget::logHeapShape("dict.font_after_restore");
    return SETTINGS.getBuiltInReaderFontId();
  }

  char path[160] = {};
  uint8_t selectedPointSize = 0;
  if (!findInstalledFontFile(familyName, SETTINGS.getSdFontTargetPointSize(), FontFileSelection::Closest, path,
                             sizeof(path), selectedPointSize)) {
    LOG_ERR("SDFS", "Reader font unavailable while restoring: %s", familyName);
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    loadedFontPointSize_ = 0;
    MemoryBudget::logHeapShape("dict.font_after_restore");
    return SETTINGS.getBuiltInReaderFontId();
  }

  if (manager_.currentFamilyName() != familyName || manager_.currentPointSize() != selectedPointSize) {
    if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
    if (!manager_.loadFamilyFile(path, familyName, selectedPointSize, renderer)) {
      LOG_ERR("SDFS", "Failed to restore reader font: %s", familyName);
      MemoryBudget::logHeapShape("dict.font_after_restore");
      return SETTINGS.getBuiltInReaderFontId();
    }
    loadedFontPointSize_ = SETTINGS.getSdFontTargetPointSize();
  }

  const int fontId = manager_.getFontId(manager_.currentFamilyName());
  MemoryBudget::logHeapShape("dict.font_after_restore");
  return fontId != 0 ? fontId : SETTINGS.getBuiltInReaderFontId();
}

void SdCardFontSystem::markRegistryDirtyForPath(const char* path) {
  if (!path) return;
  for (const char* root : {SdCardFontRegistry::FONTS_DIR_HIDDEN, SdCardFontRegistry::FONTS_DIR_VISIBLE}) {
    const size_t length = std::strlen(root);
    if (strncasecmp(path, root, length) == 0 && (path[length] == '/' || path[length] == '\0')) {
      markRegistryDirty();
      return;
    }
  }
}
