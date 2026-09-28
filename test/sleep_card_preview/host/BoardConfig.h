#pragma once

// Host stand-in: an X4 Pro (portrait 480x800 on an 800x480 panel).
#include <cstdint>

namespace BoardConfig {
enum class DisplayController : uint8_t { SSD1677, UC8179, UC8279 };
struct ViewableInsets {
  uint8_t top = 9;
  uint8_t right = 3;
  uint8_t bottom = 3;
  uint8_t left = 3;
};
struct BoardProfile {
  ViewableInsets viewableInsets;
};
inline BoardProfile ACTIVE;
inline bool isX4Pro() { return true; }
inline bool hasTouch() { return true; }
}  // namespace BoardConfig
