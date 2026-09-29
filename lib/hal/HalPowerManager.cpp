#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>

#include <cassert>

#include "HalFrontlight.h"
#include "HalGPIO.h"
#include "PowerPolicy.h"

#if FREEINK_DEVICE_PAPERMONO
#include <M5Pm1.h>
#endif

HalPowerManager powerManager;  // Singleton instance

// GPIO13 controls the X4 battery latch and the X3 SD power rail on the C3
// Xteink boards. Other boards use it for unrelated signals, including the
// X4 Pro display chip select.
static constexpr gpio_num_t XTEINK_C3_GPIO13 = GPIO_NUM_13;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

unsigned long HalPowerManager::idlePowerSavingMs() { return power_policy::idlePowerSavingMs(BoardConfig::isX4Pro()); }

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0 || modeMutex == nullptr) {
    return;  // invalid state
  }

  xSemaphoreTake(modeMutex, portMAX_DELAY);
  // A Lock, the radio lock or a running Wi-Fi keeps the full clock.
  if (lockCount != 0 || !power_policy::mayDownclock({radioLock, WiFi.getMode() != WIFI_MODE_NULL})) {
    enabled = false;
  }

  if (enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      isLowPower = true;
    } else {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
    }
  } else if (!enabled && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (setCpuFrequencyMhz(normalFreq)) {
      isLowPower = false;
    } else {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
    }
  }
  xSemaphoreGive(modeMutex);
}

// Caller holds modeMutex.
void HalPowerManager::setCpuMhzLocked(const int mhz) {
  if (mhz <= 0 || static_cast<int>(getCpuFrequencyMhz()) == mhz) return;
  if (!setCpuFrequencyMhz(mhz)) LOG_ERR("PWR", "Failed to set CPU frequency = %d MHz", mhz);
}

void HalPowerManager::acquireRadioLock() {
  if (modeMutex == nullptr) return;
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  if (!radioLock) {
    radioLock = true;
    ++lockCount;
  }
  // Full clock before the radio starts, whatever lowered it (idle or a refresh wait).
  setCpuMhzLocked(normalFreq);
  isLowPower = false;
  refreshDownclocked = false;
  xSemaphoreGive(modeMutex);
  LOG_DBG("PWR", "Radio lock taken (%lu MHz)", static_cast<unsigned long>(getCpuFrequencyMhz()));
}

void HalPowerManager::releaseRadioLock() {
  if (modeMutex == nullptr) return;
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  if (radioLock) {
    radioLock = false;
    --lockCount;
  }
  xSemaphoreGive(modeMutex);
  LOG_DBG("PWR", "Radio lock released");
}

bool HalPowerManager::radioActive() const {
  return power_policy::radioActive({radioLock, WiFi.getMode() != WIFI_MODE_NULL});
}

void HalPowerManager::refreshWaitBegin() {
  HalPowerManager& pm = powerManager;
  if (pm.modeMutex == nullptr || pm.normalFreq <= REFRESH_WAIT_FREQ) return;
  xSemaphoreTake(pm.modeMutex, portMAX_DELAY);
  if (!pm.refreshDownclocked && !pm.isLowPower &&
      power_policy::mayDownclock({pm.radioLock, WiFi.getMode() != WIFI_MODE_NULL}) &&
      static_cast<int>(getCpuFrequencyMhz()) > REFRESH_WAIT_FREQ) {
    pm.refreshDownclocked = setCpuFrequencyMhz(REFRESH_WAIT_FREQ);
  }
  xSemaphoreGive(pm.modeMutex);
}

void HalPowerManager::refreshWaitEnd() {
  HalPowerManager& pm = powerManager;
  if (pm.modeMutex == nullptr) return;
  xSemaphoreTake(pm.modeMutex, portMAX_DELAY);
  if (pm.refreshDownclocked) {
    pm.setCpuMhzLocked(pm.normalFreq);
    pm.refreshDownclocked = false;
  }
  xSemaphoreGive(pm.modeMutex);
}

bool HalPowerManager::lightSleep(HalGPIO& gpio, const LightSleepContext& ctx) {
  const auto& board = BoardConfig::ACTIVE;
  const int8_t stat = board.batteryChargeStatus;
  const int statActive = board.batteryChargeStatusActiveHigh ? HIGH : LOW;

  power_policy::SleepInputs in;
  in.enabled = lightSleepOn;
  in.boardSupports = BoardConfig::isX4Pro();
  if (in.enabled && in.boardSupports) {
    // Only the main loop takes Locks besides the render task, which takes its
    // Lock after the RenderLock the caller holds: an unlocked read is exact.
    // The radio lock is counted too; it reports as the radio, not a Lock.
    in.powerLockHeld = power_policy::otherLockHeld(lockCount, radioLock);
    in.radio = {radioLock, WiFi.getMode() != WIFI_MODE_NULL};
    in.usbHost = ctx.usbHost;
    in.charging = gpio.isUsbConnected() || (stat >= 0 && digitalRead(stat) == statActive);
    in.frontlightLit = Frontlight.present() && Frontlight.isOn();
    in.frontlightSurvivesSleep = Frontlight.survivesLightSleep();
    in.inputActive = gpio.rawInputActive() || gpio.touchActive();
    in.debouncePending = gpio.isDebouncePending();
    in.activityBusy = ctx.activityBusy;
#if CROSSPOINT_BENCH_CONSOLE
    if (forceArmed) {
      const auto now = static_cast<uint32_t>(millis());
      in.benchForced = power_policy::before(now, forceUntilMs);
      if (!in.benchForced) {
        forceArmed = false;
        postWakeUntilMs = now + power_policy::BENCH_FORCE_TAIL_MS;
        postWakeArmed = true;
      }
    }
#endif
    in.inPostWakeWindow = postWakeArmed && power_policy::before(static_cast<uint32_t>(millis()), postWakeUntilMs);
    in.renderQueued = ctx.renderQueued;
  }
  const power_policy::Block block = power_policy::lightSleepBlock(in);
  lastBlock = static_cast<uint8_t>(block);
  if (block != power_policy::Block::None) return false;
  postWakeArmed = false;

  // Level wake on the page keys (active-LOW, INPUT_PULLUP), the power key at
  // its declared polarity, and STAT going active so plugging a charger in wakes
  // the chip. None is at its wake level here (inputActive / charging above), so
  // the sleep cannot bounce.
  const auto& keys = board.input;
  for (const int8_t pin : {keys.up, keys.down}) {
    if (pin >= 0) gpio_wakeup_enable(static_cast<gpio_num_t>(pin), GPIO_INTR_LOW_LEVEL);
  }
  if (keys.power >= 0) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(keys.power),
                       keys.powerActiveHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  }
  // A forced nap may start with STAT already active: its level wake would end
  // every nap at once, so it is not armed then.
  if (stat >= 0 && !(in.benchForced && digitalRead(stat) == statActive)) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(stat), statActive == HIGH ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(LIGHT_SLEEP_SLICE_MS) * 1000ULL);

#if CROSSPOINT_BENCH_CONSOLE
  Frontlight.napProbeArm();
#endif
  const int64_t t0 = esp_timer_get_time();
  const esp_err_t err = esp_light_sleep_start();
  const int64_t slept = esp_timer_get_time() - t0;
#if CROSSPOINT_BENCH_CONSOLE
  Frontlight.napProbeCheck(err == ESP_OK ? slept : 0);
#endif
  // Every source that fired: the timer can land beside a key, and the single
  // cause reports only the timer then.
  const uint32_t causes = esp_sleep_get_wakeup_causes();

  // Nothing armed here may leak into startDeepSleep()'s wake setup, and no pin
  // keeps a live level interrupt type.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (const int8_t pin : {keys.up, keys.down, keys.power, stat}) {
    if (pin < 0) continue;
    gpio_wakeup_disable(static_cast<gpio_num_t>(pin));
    gpio_set_intr_type(static_cast<gpio_num_t>(pin), GPIO_INTR_DISABLE);
  }
#if SOC_PM_SUPPORT_RTC_PERIPH_PD
  // Arming GPIO wake turns the RTC_PERIPH domain from AUTO to ON for good
  // (sleep_modes.c get_power_down_flags()); hand it back so the deep sleep
  // keeps the configuration its overnight drain was measured with.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO);
#endif

  if (err != ESP_OK) {
    lastBlock = static_cast<uint8_t>(power_policy::Block::Refused);
    LOG_DBG("PWR", "Light sleep refused: %d", static_cast<int>(err));
    return false;
  }
  ++lsCount;
  if (slept > 0) lsUs += static_cast<uint64_t>(slept);
  if (causes & (1UL << ESP_SLEEP_WAKEUP_GPIO)) {
    ++lsGpioWakes;
    postWakeUntilMs = static_cast<uint32_t>(millis()) + power_policy::POST_WAKE_AWAKE_MS;
    postWakeArmed = true;
  }
  return true;
}

const char* HalPowerManager::lightSleepBlockName() const {
  return power_policy::blockName(static_cast<power_policy::Block>(lastBlock));
}

#if CROSSPOINT_BENCH_CONSOLE
void HalPowerManager::forceLightSleepFor(const uint32_t seconds) {
  forceUntilMs = static_cast<uint32_t>(millis()) + seconds * 1000UL;
  forceArmed = true;
  LOG_INF("PWR", "Bench: naps forced for %lu s", static_cast<unsigned long>(seconds));
}
#endif

void HalPowerManager::startDeepSleep(HalGPIO& gpio, const uint64_t powerOffAfterUs) const {
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 gates the battery MOSFET on both Xteink C3 boards; driving it low
    // is the battery power-off (the SDK wake source still handles USB power).
    // Release any surviving pad hold first: hold_en survives deep sleep via
    // the SDK's deepSleep() (esp_sleep_config_gpio_isolate +
    // gpio_deep_sleep_hold_en), and a held pad silently ignores the drive.
    gpio_hold_dis(XTEINK_C3_GPIO13);
    gpio_set_direction(XTEINK_C3_GPIO13, GPIO_MODE_OUTPUT);
    gpio_set_level(XTEINK_C3_GPIO13, 0);
    gpio_hold_en(XTEINK_C3_GPIO13);
  }
#endif

  // Hold every configured power-latch pin HIGH through deep sleep. These are
  // keep-alive enables (the X4 Pro's master peripheral rail on GPIO1, the
  // Sticky's PWR_HOLD/PWR_LOCK): deepSleep() isolates all pads
  // (esp_sleep_config_gpio_isolate), so a latch without an armed hold loses its
  // output driver and floats — on the X4 Pro the latch drops as soon as
  // external power leaves (serial/pogo adapter unplugged), and the next power-
  // button press cold-boots instead of fast-waking. holdPowerRails() asserted
  // the latches at boot but arms no sleep hold; arm it here instead. Skips
  // XTEINK_C3_GPIO13: it IS power.latch0 on the C3 Xteink boards, where the
  // block above drives it LOW on purpose (battery power-off).
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0 || static_cast<gpio_num_t>(pin) == XTEINK_C3_GPIO13) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    // Release any surviving pad hold first: a held pad silently ignores the
    // drive below (same trap as the GPIO13 block above).
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }

  // The frontlight driver IC hangs off the rail held HIGH above (X4 Pro GPIO1).
  // deepSleep() isolates the LEDC pads (GPIO8 cool / GPIO9 warm), leaving a
  // powered driver with floating inputs. Hand the pads back from LEDC, drive
  // them to the OFF level and hold them; HalFrontlight::begin() releases the
  // hold at boot.
  {
    // Return the light-sleep XTAL request first: its count lives in RTC memory
    // and would otherwise outlive this sleep (HalFrontlight::releaseForDeepSleep()).
    Frontlight.releaseForDeepSleep();
    const auto& fl = BoardConfig::ACTIVE.frontlight;
    for (const int8_t pin : {fl.gpio, fl.gpioWarm}) {
      if (pin < 0) continue;
      const auto g = static_cast<gpio_num_t>(pin);
      ledcDetach(pin);
      gpio_hold_dis(g);  // a held pad silently ignores the drive below
      pinMode(pin, OUTPUT);
      digitalWrite(pin, fl.activeHigh ? LOW : HIGH);
      gpio_hold_en(g);
    }
  }

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

#if FREEINK_DEVICE_PAPERMONO
  // Its power button is behind the M5PM1 PMIC rather than an ESP GPIO, so
  // normal GPIO deep sleep would have no wake source. Ask the PMIC to shut the
  // device down; a button click then restarts it through a cold boot.
  if (freeink::m5pm1::requestShutdown()) {
    delay(1000);  // allow the PMIC firmware time to drop power
  }
#endif

  // Auto power-off: the timer wake lands at the top of setup(), which cuts the
  // rail (cutRailAndSleep) instead of booting.
  if (powerOffAfterUs > 0) esp_sleep_enable_timer_wakeup(powerOffAfterUs);

  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void HalPowerManager::cutRailAndSleep() const {
  const auto& b = BoardConfig::ACTIVE;
  // The sleep that armed the timer held the rail-fed enables at their OFF
  // levels and the EPD reset HIGH (the SDK keeps it HIGH while the panel rail
  // is powered). Once the rail is gone every one of those is a 3.3 V output
  // into an unpowered chip: back-power through its protection diode, the
  // milliamp-level drain the SDK's powerDownRailsForSleep() describes. Reset
  // goes LOW (the SDK's rule for a switched-off rail); the enables float
  // (INPUT now, isolated by deepSleep()).
  if (b.display.rst >= 0) {
    const auto g = static_cast<gpio_num_t>(b.display.rst);
    gpio_hold_dis(g);
    pinMode(b.display.rst, OUTPUT);
    digitalWrite(b.display.rst, LOW);
    gpio_hold_en(g);
  }
  for (const int8_t pin : {b.touch.powerEnable, b.sd.powerEnable, b.mic.enable}) {
    if (pin < 0) continue;
    gpio_hold_dis(static_cast<gpio_num_t>(pin));
    pinMode(pin, INPUT);
  }

  const int8_t latch = b.power.latch0;
  if (latch >= 0) {
    const auto g = static_cast<gpio_num_t>(latch);
    gpio_hold_dis(g);  // held HIGH through the sleep that armed the timer
    pinMode(latch, OUTPUT);
    digitalWrite(latch, LOW);
    gpio_hold_en(g);
    delay(200);  // a battery-latched board dies here; USB power falls through
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}

namespace {
const BatteryMonitor& batteryMonitor() {
  static const BatteryMonitor battery;
  return battery;
}
}  // namespace

uint16_t HalPowerManager::getBatteryMillivolts() const { return batteryMonitor().readMillivolts(); }

uint16_t HalPowerManager::getBatteryPercentage() const {
  const BatteryMonitor& battery = batteryMonitor();
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Counted: the render task's Lock and the radio lock overlap routinely.
  if (powerManager.lockCount == UINT8_MAX) {
    LOG_ERR("PWR", "Too many locks held, ignore");
    valid = false;
  } else {
    ++powerManager.lockCount;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid && powerManager.lockCount > 0) {
    --powerManager.lockCount;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
