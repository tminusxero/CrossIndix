#pragma once

#include <BoardConfig.h>
#include <ScriptBlock.h>
#include <ScriptUiFontCache.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>

#include "ReaderFontSizeStep.h"

class GfxRenderer;

struct DictionaryFontActivation {
  int fontId = 0;
  bool usingDictionaryFont = false;
};

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  using SettingsPersistenceCallback = void (*)(void* context);

  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Register the font resolver and load a saved SD font selection. When the
  /// built-in font is selected, discovery stays deferred until font metadata
  /// is explicitly requested.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer);

  // An EPUB can own a temporary per-book settings snapshot while this system
  // repairs a missing font selection. Let that reader persist its own state.
  void setSettingsPersistenceCallback(SettingsPersistenceCallback callback, void* context) {
    settingsPersistenceCallback_ = callback;
    settingsPersistenceContext_ = context;
  }

  /// Temporarily unload the active SD font without clearing the saved setting.
  /// Call ensureLoaded() later to restore it before reader rendering.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Release all SD-font RAM that network/TLS work does not need.
  void releaseForNetwork(GfxRenderer& renderer);

  /// Ensure the font catalog is available for settings/web enumeration, including
  /// newly uploaded or deleted fonts visible in the web UI. Re-indexes which
  /// installed family covers which script after every discovery.
  void ensureRegistry();

  /// SD font id to draw a UI string in script `block` at the size of the
  /// built-in UI font `primaryFontId` (SMALL/UI_10/UI_12). Loads the file
  /// lazily; before a load, least recently drawn UI fonts are unloaded while free
  /// heap is under MemoryBudget::UI_SD_FONT_EVICT_BELOW_FREE.
  /// Returns 0 when no installed family covers the script, the size is not
  /// installed, fallbacks are suspended, or the heap is too low.
  int uiFontFor(ScriptBlock block, int primaryFontId, GfxRenderer& renderer);

  /// Family indexed for `block` ("" when none). Names survive releaseRegistry().
  const char* familyForScript(ScriptBlock block) const;

  /// On a book open, unload the UI script families other than the reader's own
  /// family: a family kept for the library list (Bengali titles while reading a
  /// Hindi book) would hold its cluster table and font objects through layout,
  /// where the heap is tightest. They reload lazily when a screen needs them.
  /// Same on every device.
  void dropUiFamiliesForBook(GfxRenderer& renderer);

  /// Drop resident UI script fonts and stop lazy loading until
  /// resumeUiFallbacks(). Used around network/TLS work.
  void suspendUiFallbacks(GfxRenderer& renderer);
  void resumeUiFallbacks() { uiFallbackSuspended_ = false; }

  /// Make sure the active reader font can draw the script of `text` (a book
  /// title). When it cannot and an installed family covers that script, that
  /// family is selected for this reader session (SETTINGS.sdFontFamilyName is
  /// changed in memory only; the per-book restore on exit puts the global
  /// value back). Returns false when the text stays uncovered.
  bool ensureReaderFontCovers(GfxRenderer& renderer, const char* text);

  // Families that can be resident for UI script fallback at once: the slot
  // count only. What actually stays is decided by free heap in uiFontFor(),
  // the same on every device (MemoryBudget::UI_SD_FONT_EVICT_BELOW_FREE).
  static constexpr uint8_t kMaxResidentUiFamilies = ScriptUiFontCache::kMaxEntries;

  /// Release catalog names and paths without unloading the active reader font.
  void releaseRegistry();

  /// Resolve an SD card font ID from family name + selected point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Change the reader font size using the active SD family when one is selected.
  bool changeReaderFontSize(bool larger, FontSizeStepMode mode = FontSizeStepMode::Wrap);

  /// Convert a pre-point-size SD font slot into the exact installed size.
  /// Used while reading legacy per-book reader settings.
  uint8_t resolveLegacySizeStep(const char* familyName, uint8_t sizeStep);

  // Temporarily replace the reader SD font with a per-book dictionary font.
  // At most one SD font family remains resident. Missing or failed dictionary
  // fonts leave the reader font active and retain the saved selection.
  // targetPointSize of zero follows the active reader size. The selected file
  // still uses the closest installed size, so a removed font file degrades
  // safely without retaining another family or size cache in RAM.
  DictionaryFontActivation activateDictionaryFont(GfxRenderer& renderer, const char* familyName,
                                                  uint8_t targetPointSize = 0);

  // Restore the saved reader font after a dictionary lookup and return its ID.
  int restoreReaderFont(GfxRenderer& renderer);

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() {
    registryDirty_.store(true, std::memory_order_release);
    SdCardFontRegistry::invalidateIndex();
  }
  void markRegistryDirtyForPath(const char* path);

  /// Ensure the registry is available and re-scan it after SD changes.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() { ensureRegistry(); }

 private:
  void persistSettingsChange() const;

  // Fill scriptFamilies_/scriptUiSizes_ from the loaded registry: for every
  // family that ships at least one built-in UI size, probe the script letters
  // of ScriptBlock.h against its interval table (no font load). The first
  // family covering a script wins; the reader family always wins its scripts.
  void rebuildScriptIndex();
  void dropUiFonts(GfxRenderer& renderer);
  static int resolveUiFontTrampoline(void* ctx, uint8_t block, int primaryFontId);

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};
  bool registryLoaded_ = false;
  uint8_t loadedFontPointSize_ = 0;
  bool fontReloadPending_ = false;
  uint32_t loadedRegistryRevision_ = 0;
  SettingsPersistenceCallback settingsPersistenceCallback_ = nullptr;
  void* settingsPersistenceContext_ = nullptr;

  static constexpr uint8_t kScriptCount = static_cast<uint8_t>(ScriptBlock::COUNT);
  char scriptFamilies_[kScriptCount][ScriptUiFontCache::kFamilyNameLen] = {};
  uint8_t scriptUiSizes_[kScriptCount] = {};  // bit i = kUiFontSizes[i] installed
  ScriptUiFontCache uiCache_{kMaxResidentUiFamilies};
  GfxRenderer* renderer_ = nullptr;
  bool uiFallbackSuspended_ = false;
  bool scriptIndexBuilt_ = false;
  bool uiHeapWarned_ = false;
  // Set while ensureReaderFontCovers() tries a family for one book: a failed
  // load must not persist the cleared name over the user's choice.
  bool transientFontLoad_ = false;
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
