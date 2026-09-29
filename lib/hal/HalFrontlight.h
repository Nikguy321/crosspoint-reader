#pragma once

#include <FrontlightManager.h>

// Thin firmware HAL over the SDK frontlight manager. It is inert on boards
// without a frontlight, so callers do not need board-specific conditionals.
class HalFrontlight {
 public:
  static HalFrontlight& getInstance() { return instance; }

  void begin(uint8_t brightness, uint8_t warmth, bool on);

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }

  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  bool isOn() const { return lit; }

  // True when the PWM keeps running through an idle light sleep, so a lit light
  // need not stop the nap. X4 Pro only: its LEDC timer runs from the 40 MHz
  // crystal, which this HAL keeps powered through light sleep while lit. A light
  // at duty 0 (off, or on at brightness 0) is a steady LOW either way.
  bool survivesLightSleep() const { return sleepCapable && (sleepClockHeld || !lit || lastBrightness == 0); }

  // Returns the light-sleep XTAL request before deep sleep (the light itself is
  // parked by HalPowerManager::startDeepSleep()). The IDF keeps the request
  // count in RTC slow memory, which a deep-sleep wake does not reload: a request
  // left here would keep XTAL on through every dark nap after the wake.
  void releaseForDeepSleep() { holdSleepClock(false); }

  // The IDF's live count of light-sleep XTAL requests (0 or 1 from this HAL);
  // -1 where this HAL never takes one.
  int32_t sleepClockRequests() const;

#if CROSSPOINT_BENCH_CONSOLE
  // Dev-only check that the PWM really ran through a nap: napProbeArm() right
  // before the sleep resets the lit channel's LEDC overflow counter (1024 PWM
  // periods = 40.96 ms at 25 kHz), napProbeCheck() after it counts a nap of at
  // least NAP_PROBE_MIN_US as ran when the counter reached 1024. A frozen PWM
  // leaves it near 0 (only the awake entry/exit time counts).
  static constexpr int64_t NAP_PROBE_MIN_US = 45000;
  void napProbeArm();
  void napProbeCheck(int64_t sleptUs);
  uint32_t napProbeChecked() const { return probeChecked; }
  uint32_t napProbeRan() const { return probeRan; }
#endif

 private:
  HalFrontlight() = default;

  // Writes the live duty (the brightness while lit, else 0). XTAL is held
  // through light sleep exactly while that duty is nonzero: taken before the
  // first nonzero write, returned after the zero one.
  void applyBrightness();
  void holdSleepClock(bool hold);

  FrontlightManager manager;
  // The SDK represents off as brightness 0. Keep the selected brightness so
  // toggling back on restores it.
  uint8_t lastBrightness = 60;
  bool lit = false;
  bool sleepCapable = false;    // decided in begin()
  bool sleepClockHeld = false;  // our reference on the light-sleep XTAL keep-on
#if CROSSPOINT_BENCH_CONSOLE
  int8_t probeChannel = -1;  // LEDC channel armed for this nap, -1 none
  uint32_t probeChecked = 0;
  uint32_t probeRan = 0;
#endif

  static HalFrontlight instance;
};

#define Frontlight HalFrontlight::getInstance()
