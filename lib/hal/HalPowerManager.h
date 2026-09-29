#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  // Holders of a full-clock lock: every live Lock plus the radio lock. Counted,
  // so the render task's Lock and a radio session's lock can overlap.
  uint8_t lockCount = 0;
  bool radioLock = false;
  // The refresh BUSY wait dropped the clock to REFRESH_WAIT_FREQ (render task).
  bool refreshDownclocked = false;
  SemaphoreHandle_t modeMutex = nullptr;  // Protects the fields above and every clock change

  // Idle light sleep (X4 Pro). Counters are read by the power ledger and STATE.
  bool lightSleepOn = true;  // bench "LS on|off"; RAM only
  uint32_t lsCount = 0;
  uint32_t lsGpioWakes = 0;  // naps ended by a key or STAT (the rest end on the timer)
  uint64_t lsUs = 0;
  uint32_t postWakeUntilMs = 0;
  bool postWakeArmed = false;
  uint8_t lastBlock = 0;  // power_policy::Block of the last lightSleep() call
#if CROSSPOINT_BENCH_CONSOLE
  bool forceArmed = false;  // bench LSFORCE window open
  uint32_t forceUntilMs = 0;
#endif

  void setCpuMhzLocked(int mhz);

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long BATTERY_POLL_MS = 1500;  // ms
  // One idle light sleep: the timer slice that keeps today's 50 ms touch poll.
  static constexpr uint32_t LIGHT_SLEEP_SLICE_MS = 50;
  // CPU clock through a long e-paper BUSY wait; APB stays 80 MHz, so SPI,
  // I2C and LEDC timing are unchanged.
  static constexpr int REFRESH_WAIT_FREQ = 80;  // MHz

  void begin();

  // Idle time before the loop drops the clock: 1 s on the X4 Pro (where it also
  // starts light-sleeping, as stock does), 3 s elsewhere (power_policy).
  static unsigned long idlePowerSavingMs();

  // Control CPU frequency for power saving. Never below full clock while a
  // Lock or the radio lock is held, or while Wi-Fi is on.
  void setPowerSaving(bool enabled);

  // The radio lock: full clock and no light sleep from before a radio starts
  // until it is off. Taken and released only by RadioPower (src/network), the
  // one owner of every radio start (scripts/check_radio_power.py).
  void acquireRadioLock();
  void releaseRadioLock();
  bool radioLocked() const { return radioLock; }
  // Radio lock held, or Wi-Fi up by any route.
  bool radioActive() const;

  // Idle light sleep for one LIGHT_SLEEP_SLICE_MS timer slice, woken early by
  // a page key, the power key or the charger STAT line. X4 Pro only; refuses
  // (returns false) under any power_policy::Block. Call from the main loop with
  // the RenderLock held so no render, SPI or SD transfer is in flight.
  struct LightSleepContext {
    bool usbHost = false;       // a computer on USB (SOF frames)
    bool activityBusy = false;  // preventAutoSleep() / skipLoopDelay()
    bool renderQueued = false;  // a render requested but not yet taken
  };
  bool lightSleep(HalGPIO& gpio, const LightSleepContext& ctx);
  void setLightSleepEnabled(bool on) { lightSleepOn = on; }
  bool lightSleepEnabled() const { return lightSleepOn; }
  uint32_t lightSleepCount() const { return lsCount; }
  uint32_t lightSleepGpioWakes() const { return lsGpioWakes; }
  // Time inside esp_light_sleep_start(), entry and exit overhead included.
  uint64_t lightSleepMicros() const { return lsUs; }
  // Why the last lightSleep() call did not sleep ("none" when it slept,
  // "refused" when the SoC rejected the sleep).
  const char* lightSleepBlockName() const;
#if CROSSPOINT_BENCH_CONSOLE
  // Bench LSFORCE: for `seconds` the naps ignore the USB-host and charger
  // guards (the USB pad drops meanwhile), then the chip stays awake
  // power_policy::BENCH_FORCE_TAIL_MS so the host re-enumerates.
  void forceLightSleepFor(uint32_t seconds);
#endif

  // EpdBus busy-wait hooks (render task): 80 MHz through a long refresh wait
  // unless a radio is active. Installed by HalDisplay::begin() on the X4 Pro.
  static void refreshWaitBegin();
  static void refreshWaitEnd();

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the lock count
  // powerOffAfterUs > 0 also arms a timer wake beside the button; setup() turns
  // that wake into cutRailAndSleep() (stock-style auto power-off).
  void startDeepSleep(HalGPIO& gpio, uint64_t powerOffAfterUs = 0) const;

  // Stock "cutLdo": drop power.latch0 (the X4 Pro's peripheral rail on GPIO1),
  // hold it LOW, and sleep on the power button only. On battery the board dies
  // here and the button cold-boots it; on USB power the SoC survives and sleeps.
  [[noreturn]] void cutRailAndSleep() const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // Cell voltage, uncached: the gauge's VCELL (CW2017: ~0.3 mV/LSB) or the ADC
  // divider reading; 0 on an I2C failure.
  uint16_t getBatteryMillivolts() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
