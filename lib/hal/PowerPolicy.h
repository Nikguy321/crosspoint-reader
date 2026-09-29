#pragma once

#include <cstdint>

// The pure decisions behind HalPowerManager's awake power saving: whether the
// idle loop may light-sleep, and whether the CPU may run below its full clock.
// No Arduino or IDF types, so a host test pins them (test/power_policy).
namespace power_policy {

// Any radio that is initialised (Wi-Fi now, BLE later) keeps the full clock and
// the chip awake: a clock change under a live radio drops its link.
struct RadioState {
  bool radioLocked = false;  // HalPowerManager's radio lock (taken by RadioPower before a start)
  bool wifiModeOn = false;   // WiFi.getMode() != WIFI_MODE_NULL
};

constexpr bool radioActive(const RadioState& r) { return r.radioLocked || r.wifiModeOn; }

// Idle 80 MHz and the 80 MHz refresh BUSY wait both ask this.
constexpr bool mayDownclock(const RadioState& r) { return !radioActive(r); }

// Why the idle loop did not light-sleep, first match wins. Names are what the
// bench console's STATE lsblk= reports.
enum class Block : uint8_t {
  None,          // sleeps
  Disabled,      // bench "LS off"
  Board,         // not an X4 Pro
  Lock,          // a HalPowerManager::Lock is held (a render, deep-sleep prep)
  Radio,         // radio locked or Wi-Fi mode on
  Host,          // a USB host is attached (the USB pad powers down in light sleep)
  Charging,      // charger STAT active (USB power): its enumeration must not see naps
  Light,         // frontlight lit: LEDC stops in light sleep
  Input,         // a key contact, touch contact or debounce is in progress
  Activity,      // the activity asks for full cadence (preventAutoSleep / skipLoopDelay)
  PostWake,      // inside the awake window after a key or STAT wake
  RenderQueued,  // a render is requested but the render task has not finished it yet
  Refused,       // every guard passed, but esp_light_sleep_start() returned an error
};

struct SleepInputs {
  bool enabled = true;
  bool boardSupports = false;
  bool powerLockHeld = false;
  RadioState radio;
  bool usbHost = false;
  bool charging = false;
  bool frontlightLit = false;
  bool inputActive = false;
  bool debouncePending = false;
  bool activityBusy = false;
  bool inPostWakeWindow = false;
  bool renderQueued = false;
};

constexpr Block lightSleepBlock(const SleepInputs& in) {
  if (!in.enabled) return Block::Disabled;
  if (!in.boardSupports) return Block::Board;
  if (in.powerLockHeld) return Block::Lock;
  if (radioActive(in.radio)) return Block::Radio;
  if (in.usbHost) return Block::Host;
  if (in.charging) return Block::Charging;
  if (in.frontlightLit) return Block::Light;
  if (in.inputActive || in.debouncePending) return Block::Input;
  if (in.activityBusy) return Block::Activity;
  if (in.inPostWakeWindow) return Block::PostWake;
  if (in.renderQueued) return Block::RenderQueued;
  return Block::None;
}

constexpr bool shouldLightSleep(const SleepInputs& in) { return lightSleepBlock(in) == Block::None; }

constexpr const char* blockName(const Block b) {
  switch (b) {
    case Block::None:
      return "none";
    case Block::Disabled:
      return "off";
    case Block::Board:
      return "board";
    case Block::Lock:
      return "lock";
    case Block::Radio:
      return "radio";
    case Block::Host:
      return "host";
    case Block::Charging:
      return "charging";
    case Block::Light:
      return "light";
    case Block::Input:
      return "input";
    case Block::Activity:
      return "activity";
    case Block::PostWake:
      return "postwake";
    case Block::RenderQueued:
      return "render";
    case Block::Refused:
      return "refused";
  }
  return "?";
}

// A HalPowerManager::Lock other than the radio lock (both are counted in one
// lockCount), so a Wi-Fi session reports Block::Radio rather than Block::Lock.
constexpr bool otherLockHeld(const uint8_t lockCount, const bool radioLock) { return lockCount > (radioLock ? 1 : 0); }

// How long the loop idles before it drops the clock (and, on the X4 Pro, starts
// light-sleeping). Stock X4 Pro firmware light-sleeps after 1 s; the C3 boards
// keep 3 s, since their 10 MHz step also changes APB and fires every
// APB-change callback.
constexpr uint32_t IDLE_POWER_SAVING_MS = 3000;
constexpr uint32_t IDLE_POWER_SAVING_LIGHT_SLEEP_MS = 1000;
constexpr uint32_t idlePowerSavingMs(const bool lightSleepBoard) {
  return lightSleepBoard ? IDLE_POWER_SAVING_LIGHT_SLEEP_MS : IDLE_POWER_SAVING_MS;
}

// After a key or STAT wake the chip stays awake this long: a USB host needs the
// pad up to enumerate, and a second tap lands on a running loop.
constexpr uint32_t POST_WAKE_AWAKE_MS = 3000;

// Wrap-safe "now is before until" on a millis() clock.
constexpr bool before(const uint32_t nowMs, const uint32_t untilMs) {
  return static_cast<int32_t>(untilMs - nowMs) > 0;
}

// A USB host seen through the SOF-based "plugged" reading: present at once,
// gone only after ABSENT_MS without a reading (a reading can blip false).
class HostSeen {
 public:
  static constexpr uint32_t ABSENT_MS = 2000;
  void update(const uint32_t nowMs, const bool plugged) {
    if (!plugged) return;
    seen = true;
    lastSeenMs = nowMs;
  }
  bool present(const uint32_t nowMs) const { return seen && nowMs - lastSeenMs < ABSENT_MS; }

 private:
  uint32_t lastSeenMs = 0;
  bool seen = false;
};

}  // namespace power_policy
