#pragma once

#include <cstdint>

// Live sleep (X4 Pro): on external power the sleep screen stays up instead of deep sleeping. It is
// redrawn on the minute every Charging Updates interval (the footer's time and battery stay true),
// optionally deals a new Shuffle card each time (Card Cycle When Charging), and a station keeper
// holds Wi-Fi. Unplugging ends it in today's deep sleep. Pure decisions only; main.cpp and
// SleepActivity own the hardware side, and a host test pins these (test/live_sleep).
namespace live_sleep {

// The board has no VBUS sense: power is the charger STAT line (charging; it drops at charge
// termination with the cable still in) or a computer's USB SOF frames (a host proves VBUS).
constexpr bool externalPower(const bool stat, const bool usbHost) { return stat || usbHost; }

// CrossPointSettings::SLEEP_SCREEN_MODE values of the cards, NOW_READING .. SHUFFLE
// (static_assert'ed against the enum in main.cpp; this header stays free of the settings store).
constexpr uint8_t FIRST_CARD_MODE = 8;
constexpr uint8_t LAST_CARD_MODE = 14;
constexpr bool isCardMode(const uint8_t sleepMode) {
  return sleepMode >= FIRST_CARD_MODE && sleepMode <= LAST_CARD_MODE;
}

// A sleep stays live on the X4 Pro on external power when it shows a card, or for any sleep
// screen when Card Cycle When Charging is on (the cycle deals Shuffle cards whatever the mode).
constexpr bool liveEligible(const bool isX4Pro, const bool externalPowerPresent, const uint8_t sleepMode,
                            const bool cycleOn) {
  return isX4Pro && externalPowerPresent && (cycleOn || isCardMode(sleepMode));
}

// External power absent this long without a break counts as unplugged. The charger STAT line
// may drop at charge termination with the cable still in: such a false unplug ends in today's
// deep sleep, which is safe.
constexpr uint32_t UNPLUG_DEBOUNCE_MS = 20000;

// Fed one power sample per loop pass: any present sample restarts the count.
class UnplugDebounce {
 public:
  void start(const uint32_t nowMs) {
    absent_ = false;
    since_ = nowMs;
  }
  void note(const bool present, const uint32_t nowMs) {
    if (present) {
      absent_ = false;
    } else if (!absent_) {
      absent_ = true;
      since_ = nowMs;
    }
  }
  bool unplugged(const uint32_t nowMs) const { return absent_ && nowMs - since_ >= UNPLUG_DEBOUNCE_MS; }
  // How long power has been absent (0 while present).
  uint32_t absentMs(const uint32_t nowMs) const { return absent_ ? nowMs - since_ : 0; }

 private:
  bool absent_ = false;
  uint32_t since_ = 0;
};

// Charging Updates (CrossPointSettings::chargingUpdateInterval), persisted by index: append only.
constexpr uint8_t INTERVAL_COUNT = 5;
constexpr uint8_t INTERVAL_MINUTES[INTERVAL_COUNT] = {1, 2, 5, 10, 15};
constexpr uint8_t DEFAULT_INTERVAL_INDEX = 1;  // 2 min
constexpr uint8_t intervalMinutes(const uint8_t index) {
  return INTERVAL_MINUTES[index < INTERVAL_COUNT ? index : DEFAULT_INTERVAL_INDEX];
}

// A redraw lands this long after its minute turns, so the footer's minute is true for the whole
// interval (the RTC read has whole seconds).
constexpr uint32_t REDRAW_PAST_MINUTE_S = 2;

// Milliseconds from now to the next redraw: REDRAW_PAST_MINUTE_S past the next local wall-clock
// minute that is a multiple of the interval (every interval divides a day). Without a set clock,
// simply the interval.
constexpr uint32_t nextRedrawDelayMs(const bool clockValid, const uint32_t secondsOfDay, const uint8_t minutes) {
  const uint32_t interval = (minutes == 0 ? 1u : minutes) * 60u;
  if (!clockValid) return interval * 1000u;
  const uint32_t now = secondsOfDay % 86400u;
  uint32_t target = (now / interval) * interval + REDRAW_PAST_MINUTE_S;
  if (target <= now) target += interval;
  return (target - now) * 1000u;
}

// Refresh: HALF (a clean pass) when the card changes, after a picture (its grey waveform leaves
// the panel needing a clean pass) and at least every HALF_EVERY-th redraw; FAST (only the footer
// pixels change) for a same-card redraw in between. The final frame (the unplug, before deep
// sleep) is always HALF: it stays on the unpowered panel, like every other sleep frame.
constexpr uint8_t HALF_EVERY = 5;
constexpr bool halfRefresh(const bool cardChanged, const bool afterPicture, const uint8_t fastSinceHalf,
                           const bool finalFrame = false) {
  return cardChanged || afterPicture || finalFrame || fastSinceHalf + 1u >= HALF_EVERY;
}

// The wake waits for every key to be up (so the press cannot also act on what comes back), but
// no longer than this after the press: a key held down by a case or a bag must not keep the
// screen live, and the restart's boot absorbs a key still held.
constexpr uint32_t WAKE_RELEASE_WAIT_MS = 1500;
constexpr bool wakeNow(const bool anyKeyDown, const uint32_t sincePressMs) {
  return !anyKeyDown || sincePressMs >= WAKE_RELEASE_WAIT_MS;
}

}  // namespace live_sleep
