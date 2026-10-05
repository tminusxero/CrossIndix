#pragma once

#include <cstdint>
#include <string>
#include <vector>

class GfxRenderer;
class SdCardFont;
struct SdCardFontFamilyInfo;
struct SdCardFontFileInfo;

class SdCardFontManager {
 public:
  SdCardFontManager() = default;
  ~SdCardFontManager();
  SdCardFontManager(const SdCardFontManager&) = delete;
  SdCardFontManager& operator=(const SdCardFontManager&) = delete;

  // Load the file physically closest to targetPointSize. Only one .cpfont
  // file is loaded; other sizes remain on disk. This keeps resident interval
  // + kern/ligature tables to one size's worth of memory.
  bool loadFamilyClosest(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t targetPointSize);

  // Load a known file path without constructing a registry family. Used by
  // dictionary lookup to avoid allocating a whole catalog for one family.
  bool loadFamilyFile(const char* path, const char* familyName, uint8_t pointSize, GfxRenderer& renderer);

  // Additively load the .cpfont of `family` at the exact physical `pointSize`
  // (used for size-matched CJK UI fallback alongside the reader-size font).
  // Does not unload anything. If a font of that size is already loaded its id
  // is reused. Returns the font id, or 0 if the family has no file at that size
  // or loading failed.
  int loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize);

  // Add an exact extra-size file found by the fixed-buffer dictionary path.
  int loadFamilyExtraFile(const char* path, const char* familyName, uint8_t pointSize, GfxRenderer& renderer);

  // Unload the reader family (and its extra sizes), unregister from renderer.
  // UI script fonts loaded with loadUiFont() are left in place.
  void unloadAll(GfxRenderer& renderer);

  // Additively load one file of `familyName` for UI script fallback. These
  // fonts are owned separately from the reader family: unloadAll() keeps
  // them, unloadUiFamily() drops them. If the same file is already resident
  // (as a UI font or as the reader font) its id is reused. Returns the font
  // id, or 0 on failure.
  int loadUiFont(const char* path, const char* familyName, uint8_t pointSize, GfxRenderer& renderer);
  void unloadUiFamily(const char* familyName, GfxRenderer& renderer);
  // Unload one UI font by id (one size of a family); no-op for ids it does not own.
  void unloadUiFont(int fontId, GfxRenderer& renderer);
  void unloadAllUiFonts(GfxRenderer& renderer);
  uint8_t uiFontCount() const { return static_cast<uint8_t>(uiLoaded_.size()); }

  // Id of the reader family's font at exactly `pointSize`, or 0.
  int readerFontIdAt(const char* familyName, uint8_t pointSize) const;

  // Look up the font ID for the loaded family. Returns 0 if nothing loaded
  // or familyName doesn't match.
  int getFontId(const std::string& familyName) const;

  // Get name of currently loaded family (empty if none).
  const std::string& currentFamilyName() const { return loadedFamilyName_; };

  // Point size that was actually loaded (closest match to targetPtSize).
  // 0 if nothing loaded.
  uint8_t currentPointSize() const { return loadedPointSize_; };

 private:
  struct LoadedFont {
    SdCardFont* font;  // heap-allocated, owned
    int fontId;
    uint8_t size;
  };
  static int computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize);

  // Load+register a single .cpfont file and append it to loaded_.
  // Returns the font id, or 0 on failure (allocation, read, or id collision).
  int loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer);
  int loadFilePath(const char* path, const char* familyName, uint8_t pointSize, GfxRenderer& renderer);

  struct UiFont {
    SdCardFont* font;  // heap-allocated, owned
    int fontId;
    uint8_t size;
    std::string family;
  };
  // Move a UI-owned font of the same file into loaded_ so the reader can use
  // it without a second copy (and without a font id collision).
  int adoptUiFont(const char* familyName, uint8_t pointSize);

  std::string loadedFamilyName_;
  uint8_t loadedPointSize_ = 0;
  std::vector<LoadedFont> loaded_;
  std::vector<UiFont> uiLoaded_;
};
