#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
  for (auto& uf : uiLoaded_) {
    delete uf.font;
  }
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

int SdCardFontManager::loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer) {
  return loadFilePath(file.path.c_str(), familyName, file.pointSize, renderer);
}

int SdCardFontManager::adoptUiFont(const char* familyName, const uint8_t pointSize) {
  for (auto it = uiLoaded_.begin(); it != uiLoaded_.end(); ++it) {
    if (it->size != pointSize || it->family != familyName) continue;
    const int fontId = it->fontId;
    loaded_.push_back({it->font, fontId, pointSize});
    uiLoaded_.erase(it);
    LOG_DBG("SDMGR", "Reader adopted UI font %s %u pt id=%d", familyName, pointSize, fontId);
    return fontId;
  }
  return 0;
}

int SdCardFontManager::loadFilePath(const char* path, const char* familyName, uint8_t pointSize,
                                    GfxRenderer& renderer) {
  if (const int adopted = adoptUiFont(familyName, pointSize)) return adopted;
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", path);
    return 0;
  }

  if (!font->load(path)) {
    LOG_ERR("SDMGR", "Failed to load %s", path);
    delete font;
    return 0;
  }

  int fontId = computeFontId(font->contentHash(), familyName, pointSize);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, path);
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", path, pointSize, fontId, font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  return fontId;
}

bool SdCardFontManager::loadFamilyClosest(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                          uint8_t targetPointSize) {
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  const SdCardFontFileInfo* selected = family.findClosestFile(targetPointSize);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files near %u pt", family.name.c_str(), targetPointSize);
    return false;
  }

  if (loadFile(*selected, family.name.c_str(), renderer) == 0) {
    return false;
  }

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

bool SdCardFontManager::loadFamilyFile(const char* path, const char* familyName, uint8_t pointSize,
                                       GfxRenderer& renderer) {
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }
  if (loadFilePath(path, familyName, pointSize, renderer) == 0) {
    return false;
  }
  loadedFamilyName_ = familyName;
  loadedPointSize_ = pointSize;
  return true;
}

int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                           uint8_t pointSize) {
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  if (!file) return 0;  // family has no .cpfont at this exact size

  // Reuse an already-loaded font of the same size (e.g. when a reader size
  // happens to match a UI size) instead of double-loading the file.
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }

  return loadFile(*file, family.name.c_str(), renderer);
}

int SdCardFontManager::loadFamilyExtraFile(const char* path, const char* familyName, uint8_t pointSize,
                                           GfxRenderer& renderer) {
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }
  return loadFilePath(path, familyName, pointSize, renderer);
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  // removeFont() also drops the renderer's SdCardFont* entry, so UI script
  // fonts owned by uiLoaded_ stay registered and usable.
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);
    delete lf.font;
  }
  loaded_.clear();
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
}

int SdCardFontManager::loadUiFont(const char* path, const char* familyName, const uint8_t pointSize,
                                  GfxRenderer& renderer) {
  if (!path || !familyName) return 0;
  for (const auto& uf : uiLoaded_) {
    if (uf.size == pointSize && uf.family == familyName) return uf.fontId;
  }
  if (const int readerId = readerFontIdAt(familyName, pointSize)) return readerId;

  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate UI SdCardFont for %s", path);
    return 0;
  }
  if (!font->load(path)) {
    LOG_ERR("SDMGR", "Failed to load UI font %s", path);
    delete font;
    return 0;
  }
  const int fontId = computeFontId(font->contentHash(), familyName, pointSize);
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "UI font ID %d collides with existing font, skipping %s", fontId, path);
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  uiLoaded_.push_back({font, fontId, pointSize, familyName});
  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);
  LOG_DBG("SDMGR", "Loaded UI font %s size=%u id=%d", path, pointSize, fontId);
  return fontId;
}

void SdCardFontManager::unloadUiFamily(const char* familyName, GfxRenderer& renderer) {
  if (!familyName) return;
  for (auto it = uiLoaded_.begin(); it != uiLoaded_.end();) {
    if (it->family == familyName) {
      renderer.removeFont(it->fontId);
      delete it->font;
      it = uiLoaded_.erase(it);
    } else {
      ++it;
    }
  }
}

void SdCardFontManager::unloadUiFont(const int fontId, GfxRenderer& renderer) {
  for (auto it = uiLoaded_.begin(); it != uiLoaded_.end(); ++it) {
    if (it->fontId == fontId) {
      renderer.removeFont(it->fontId);
      delete it->font;
      uiLoaded_.erase(it);
      return;
    }
  }
}

void SdCardFontManager::unloadAllUiFonts(GfxRenderer& renderer) {
  for (auto& uf : uiLoaded_) {
    renderer.removeFont(uf.fontId);
    delete uf.font;
  }
  uiLoaded_.clear();
}

int SdCardFontManager::readerFontIdAt(const char* familyName, const uint8_t pointSize) const {
  if (!familyName || loadedFamilyName_ != familyName) return 0;
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }
  return 0;
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return loaded_.front().fontId;
}
