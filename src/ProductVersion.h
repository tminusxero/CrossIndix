#pragma once

/// The product label shown to the user: "CrossIndix-<version>+<commit> (CrossInk <version>)".
/// Hardware builds get the pieces from scripts/git_branch.py; simulator and test builds fall
/// back to the CrossInk version they define directly. The commit (CROSSINDIX_BUILD_ID, with
/// "-dirty" when the build tree had uncommitted changes) says which build is on the device:
/// boot screen, settings footer, OTA screen, sleep screen of debug builds, and the boot log.
#ifdef CROSSINDIX_BUILD_ID
#define CROSSINDIX_BUILD_SUFFIX "+" CROSSINDIX_BUILD_ID
#else
#define CROSSINDIX_BUILD_SUFFIX ""
#endif

#if defined(CROSSINDIX_VERSION) && defined(CROSSINK_BASE_VERSION)
#define CROSSINDIX_LABEL \
  "CrossIndix-" CROSSINDIX_VERSION CROSSINDIX_BUILD_SUFFIX " (CrossInk " CROSSINK_BASE_VERSION ")"
#else
#define CROSSINDIX_LABEL "CrossIndix" CROSSINDIX_BUILD_SUFFIX " (CrossInk " CROSSINK_VERSION ")"
#endif
