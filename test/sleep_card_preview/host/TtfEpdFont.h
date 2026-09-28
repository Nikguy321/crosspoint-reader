#pragma once
// Host stand-in: vector fonts are PSRAM-board only and off on the host.
#define CROSSPOINT_VECTOR_FONTS 0
class TtfEpdFont {
 public:
  void releaseResidentCaches() {}
};
