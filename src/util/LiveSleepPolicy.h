#pragma once

#include <cstdint>

// Live sleep (X4 Pro): on external power the sleep screen stays up instead of deep sleeping. It is
// redrawn on the minute every Charging Updates interval (the footer's time and battery stay true),
// optionally deals a new Shuffle card each time (Card Cycle When Charging), and a station keeper
// holds Wi-Fi. Unplugging ends it in today's deep sleep, except at full charge, where the charger
// going idle looks the same: the full-charge hold below. Pure decisions only; main.cpp and
// SleepActivity own the hardware side, and a host test pins these (test/live_sleep).
namespace live_sleep {

// The board has no VBUS sense: power is the charger STAT line (charging; it drops at charge
// termination with the cable still in) or a computer's USB SOF frames (a host proves VBUS).
constexpr bool externalPower(const bool stat, const bool usbHost) { return stat || usbHost; }

// CrossPointSettings::SLEEP_SCREEN_MODE values of the cards, NOW_READING .. WEATHER
// (static_assert'ed against the enum in main.cpp; this header stays free of the settings store).
constexpr uint8_t FIRST_CARD_MODE = 8;
constexpr uint8_t LAST_CARD_MODE = 15;
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
// drops at charge termination with the cable still in: with a full battery that is the full-charge
// hold below; otherwise such a false unplug ends in today's deep sleep, which is safe.
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
  // Power is absent now (since the first absent sample: the moment to read the gauge's SOC).
  bool absent() const { return absent_; }
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

// ---- the full-charge hold ----------------------------------------------------------------------
// The board has no cable-present line (2026-10-03, a PINS run with the cable out ~54 s: only the
// charger STAT line followed it; the six free pins never changed). On a wall charger the charger
// stops at full, STAT drops, and the unplug debounce read that as an unplug: deep sleep, then Auto
// Power Off cut it (2026-10-01 overnight: live 18:56, `sleep x=2` 19:09 at 99 %, cut 19:38). Kept
// on a charger at 94-98 % STAT stayed low for hours (the charger restarts only below its recharge
// threshold). So an "unplug" with a full battery is held instead: the screen stays up quietly
// (Wi-Fi off, light-sleep naps, a redraw at most every HOLD_REDRAW_MIN_MINUTES), assuming the
// cable is still in. The charger starting again (STAT, or a USB host) makes it fully live again;
// the battery dropping HOLD_DROP_PCT instead means it really was unplugged, and it deep-sleeps.
// The accepted cost: a reader unplugged right at full spends ~3 % before it notices (counted from
// the SOC when power vanished, so more if the gauge crept up after that). Unproven: this assumes the
// charger restarts before the battery is HOLD_DROP_PCT down; if not, the hold ends on the charger
// (holdend x=4), and the overnight /sleep.log says where the threshold has to go.

// The gauge SOC at the moment power vanished must be at least this for the hold; below it the
// unplug is today's (final redraw, deep sleep).
constexpr unsigned HOLD_MIN_SOC = 97;
constexpr bool holdEligible(const unsigned socAtLoss) { return socAtLoss >= HOLD_MIN_SOC; }

// A drop of this much from the SOC the hold started at: it was unplugged.
constexpr unsigned HOLD_DROP_PCT = 3;
// The longest hold, so a gauge that never moves cannot keep it on forever.
constexpr uint32_t HOLD_MAX_MS = 24UL * 60UL * 60UL * 1000UL;
// Redraws while holding: every Charging Updates interval, but no more often than this.
constexpr uint8_t HOLD_REDRAW_MIN_MINUTES = 15;

// What ends a hold, checked in this order: power back (fully live again), the drop, the cap (both
// then deep sleep).
enum class HoldExit : uint8_t { Stay, PowerBack, Drop, Cap };
// /sleep.log "holdend" x= (and the "sleep" x= that follows a drop or the cap); -1 for Stay.
constexpr int holdEndReason(const HoldExit exit) {
  return exit == HoldExit::PowerBack ? 0 : exit == HoldExit::Drop ? 4 : exit == HoldExit::Cap ? 5 : -1;
}
constexpr HoldExit holdExit(const bool powerPresent, const unsigned soc, const unsigned startSoc,
                            const uint32_t heldMs) {
  if (powerPresent) return HoldExit::PowerBack;
  if (soc + HOLD_DROP_PCT <= startSoc) return HoldExit::Drop;
  if (heldMs >= HOLD_MAX_MS) return HoldExit::Cap;
  return HoldExit::Stay;
}

// The hold's redraw interval in minutes: the Charging Updates one, at least HOLD_REDRAW_MIN_MINUTES.
constexpr uint8_t holdRedrawMinutes(const uint8_t liveMinutes) {
  return liveMinutes > HOLD_REDRAW_MIN_MINUTES ? liveMinutes : HOLD_REDRAW_MIN_MINUTES;
}
// Milliseconds to the hold's next redraw (on the wall-clock grid, as nextRedrawDelayMs).
constexpr uint32_t holdRedrawDelayMs(const bool clockValid, const uint32_t secondsOfDay, const uint8_t liveMinutes) {
  return nextRedrawDelayMs(clockValid, secondsOfDay, holdRedrawMinutes(liveMinutes));
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
