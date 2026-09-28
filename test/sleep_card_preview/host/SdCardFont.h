#pragma once

// Host stand-in: the preview renders built-in flash fonts only, so no SD font
// is ever registered and none of these is reached. Members accept whatever
// the renderer and FontCacheManager pass.
#include <cstdint>

#include "EpdFont.h"

class SdCardFont {
 public:
  static SdCardFont* fromMissCtx(void*) { return nullptr; }
  bool isOverflowGlyph(const EpdGlyph*) const { return false; }
  const uint8_t* getOverflowBitmap(const EpdGlyph*) const { return nullptr; }
  template <typename... Args>
  int prewarm(Args&&...) {
    return 0;
  }
  template <typename... Args>
  int buildAdvanceTable(Args&&...) {
    return 0;
  }
  template <typename... Args>
  int buildAdvanceTablePacked(Args&&...) {
    return 0;
  }
  bool hasAdvanceTable() const { return false; }
  uint16_t getAdvance(uint32_t, uint8_t) const { return 0; }
  const EpdFont* getEpdFont(uint8_t) const { return nullptr; }
  uint8_t resolveStyle(uint8_t style) const { return style; }
  void clearCache() {}
  void releaseResidentCaches() {}
  void logStats(const char*) {}
  void resetStats() {}
};
