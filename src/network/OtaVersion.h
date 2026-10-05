#pragma once

// Version and release-asset rules for CrossIndix over-the-air updates. Kept free
// of device headers so the host tests cover them (test/ota_version).
//
// CrossIndix releases are GitHub releases of tminusxero/CrossIndix tagged
// v<version>, with one image per device named crossindix-<version>-<device>.bin.
// The device compares the release tag with its own CROSSINDIX_VERSION.

#include <cstddef>
#include <cstring>

namespace OtaVersion {

constexpr size_t SEGMENT_COUNT = 4;

struct Parsed {
  int segments[SEGMENT_COUNT] = {0, 0, 0, 0};
  bool valid = false;
  // "-alpha", "-beta", "-rc" or "-dev" right after the numbers (any case, any
  // trailing text): 0.1.0-beta, v1.6.1-rc, 0.1.0-dev+0a602499. A device suffix
  // such as "-x4-pro" in CrossInk's own version strings is not a prerelease.
  bool prerelease = false;
};

inline bool isDigit(const char c) { return c >= '0' && c <= '9'; }

inline bool startsWith(const char* value, const char* prefix) {
  if (value == nullptr || prefix == nullptr) return false;
  return strncmp(value, prefix, strlen(prefix)) == 0;
}

inline bool endsWith(const char* value, const char* suffix) {
  if (value == nullptr || suffix == nullptr) return false;
  const size_t valueLength = strlen(value);
  const size_t suffixLength = strlen(suffix);
  if (suffixLength > valueLength) return false;
  return strcmp(value + valueLength - suffixLength, suffix) == 0;
}

inline bool startsWithIgnoreCase(const char* value, const char* prefix) {
  for (; *prefix != '\0'; ++value, ++prefix) {
    const char a = (*value >= 'A' && *value <= 'Z') ? static_cast<char>(*value - 'A' + 'a') : *value;
    if (a != *prefix) return false;
  }
  return true;
}

inline bool isPrereleaseSuffix(const char* rest) {
  if (rest == nullptr || rest[0] != '-') return false;
  const char* word = rest + 1;
  return startsWithIgnoreCase(word, "alpha") || startsWithIgnoreCase(word, "beta") ||
         startsWithIgnoreCase(word, "rc") || startsWithIgnoreCase(word, "dev");
}

inline Parsed parse(const char* version) {
  Parsed parsed;
  if (version == nullptr) return parsed;
  const char* p = version;
  if ((p[0] == 'v' || p[0] == 'V') && isDigit(p[1])) ++p;
  if (!isDigit(*p)) return parsed;

  size_t segmentIndex = 0;
  while (segmentIndex < SEGMENT_COUNT) {
    if (!isDigit(*p)) return parsed;
    int value = 0;
    while (isDigit(*p)) {
      value = value * 10 + (*p - '0');
      ++p;
    }
    parsed.segments[segmentIndex] = value;
    ++segmentIndex;
    if (*p != '.') break;
    ++p;
  }

  parsed.valid = true;
  parsed.prerelease = isPrereleaseSuffix(p);
  return parsed;
}

// > 0 when latest is newer than current, < 0 when older, 0 when equal or when
// either string does not parse. Equal numbers: a release beats a prerelease
// (0.1.0 is newer than 0.1.0-beta); two prereleases of the same number tie.
inline int compare(const char* latestVersion, const char* currentVersion) {
  const Parsed latest = parse(latestVersion);
  const Parsed current = parse(currentVersion);
  if (!latest.valid || !current.valid) return 0;

  for (size_t i = 0; i < SEGMENT_COUNT; ++i) {
    if (latest.segments[i] != current.segments[i]) return latest.segments[i] > current.segments[i] ? 1 : -1;
  }
  if (current.prerelease && !latest.prerelease) return 1;
  if (!current.prerelease && latest.prerelease) return -1;
  return 0;
}

// crossindix-<version>-<deviceType>.bin, with a non-empty version in between.
inline bool isFirmwareAssetFor(const char* assetName, const char* deviceType) {
  if (assetName == nullptr || deviceType == nullptr || deviceType[0] == '\0') return false;
  constexpr char prefix[] = "crossindix-";
  if (!startsWith(assetName, prefix)) return false;
  const size_t suffixLength = 1 + strlen(deviceType) + 4;  // "-" + device + ".bin"
  const size_t nameLength = strlen(assetName);
  if (nameLength <= sizeof(prefix) - 1 + suffixLength) return false;
  if (!endsWith(assetName, ".bin")) return false;
  const char* suffixStart = assetName + nameLength - suffixLength;
  if (suffixStart[0] != '-') return false;
  return strncmp(suffixStart + 1, deviceType, strlen(deviceType)) == 0;
}

}  // namespace OtaVersion
