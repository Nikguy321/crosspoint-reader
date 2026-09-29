#pragma once

#include <cstdint>

// Stock-style auto power-off: the device sleeps with its peripheral rail held
// up (fast wake), and a timer wake N minutes later cuts the rail — a true
// power-off — before sleeping again on the power button alone. Pure decisions
// only; main.cpp and HalPowerManager own the hardware side.
namespace auto_power_off {

// Minutes per CrossPointSettings::AUTO_POWER_OFF index. Persisted by index,
// so the table is append-only. 0 = Never.
constexpr uint8_t OPTION_COUNT = 6;
constexpr uint16_t MINUTES[OPTION_COUNT] = {5, 10, 20, 30, 60, 0};

constexpr uint16_t minutesForIndex(const uint8_t index) { return index < OPTION_COUNT ? MINUTES[index] : 0; }

// Only a board whose peripheral rail is a latch the firmware drives can cut it,
// and only the X4 Pro's topology has been proven to come back on the power
// button afterwards. Other boards never arm the timer.
constexpr bool canCutRail(const bool isX4Pro, const int8_t latch0) { return isX4Pro && latch0 >= 0; }

// Microseconds for esp_sleep_enable_timer_wakeup(); 0 = no timer (Never, or a
// board that cannot cut its rail).
constexpr uint64_t timerMicros(const uint8_t index, const bool railCuttable) {
  return railCuttable ? static_cast<uint64_t>(minutesForIndex(index)) * 60ULL * 1000000ULL : 0;
}

// A timer wake can only come from the timer above, so it means "cut now" —
// unless the power button fired in the same instant (esp_sleep_get_wakeup_causes()
// reports both): then the user wants the device on, and it boots. Any other
// wake (button, cold boot, USB) boots normally.
constexpr bool shouldCutRailOnWake(const bool timerWake, const bool buttonWake, const bool railCuttable) {
  return timerWake && !buttonWake && railCuttable;
}

}  // namespace auto_power_off
