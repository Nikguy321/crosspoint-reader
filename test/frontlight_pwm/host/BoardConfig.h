#pragma once

// Host stand-in: only the fields the frontlight code reads. The X4 Pro
// frontlight line matches the SDK profile; FrontlightPwmTest checks that.
#include <Arduino.h>

#define FREEINK_DEVICE_X4PRO 1
#define FREEINK_DEVICE_EEGO_A4 0
#define FREEINK_CAP_FRONTLIGHT 1
#define FREEINK_CAP_WARMLIGHT 1

namespace BoardConfig {
constexpr int8_t PIN_UNASSIGNED = -1;
enum class Board : uint8_t { XteinkX4Pro, XteinkX4 };
struct FrontlightConfig {
  int8_t gpio;
  uint32_t pwmFrequency;
  uint8_t pwmResolutionBits;
  bool activeHigh;
  int8_t gpioWarm = PIN_UNASSIGNED;
  bool viaPm1Pwm = false;
};
struct BoardProfile {
  Board board;
  FrontlightConfig frontlight;
};
constexpr FrontlightConfig X4_PRO_FRONTLIGHT = {8, 25000, 10, true, 9};
inline BoardProfile ACTIVE = {Board::XteinkX4Pro, X4_PRO_FRONTLIGHT};
inline bool isX4Pro() { return ACTIVE.board == Board::XteinkX4Pro; }
}  // namespace BoardConfig
