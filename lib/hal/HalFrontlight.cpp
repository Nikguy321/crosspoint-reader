#include "HalFrontlight.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <driver/gpio.h>
#include <soc/soc_caps.h>

// X4 Pro: the frontlight PWM keeps running through the idle light sleep. The
// Arduino 3.x LEDC core clocks every timer from XTAL wherever the chip allows it
// (esp32-hal-ledc.c LEDC_DEFAULT_CLK), and an XTAL-clocked LEDC keeps counting
// through light sleep once XTAL stays powered (the IDF driver's own KEEP_ALIVE
// uses this sub-mode: ledc.c ledc_glb_clk_set_sleep_mode()). The SDK's
// FREEINK_FRONTLIGHT_LS path owns the LEDC through the IDF driver instead.
#if FREEINK_DEVICE_X4PRO && !defined(FREEINK_FRONTLIGHT_LS) && defined(ESP_ARDUINO_VERSION_MAJOR) && \
    ESP_ARDUINO_VERSION_MAJOR >= 3 && SOC_LEDC_SUPPORT_XTAL_CLOCK
#define HAL_FRONTLIGHT_SLEEP_CLOCK 1
// esp_sleep_sub_mode_config() is the refcounted "a digital peripheral needs
// XTAL in light sleep" request. A deep sleep powers XTAL down regardless
// (sleep_modes.c get_sleep_flags()), but the count itself sits in RTC slow
// memory and survives into the next boot after a deep-sleep wake
// (esp_sleep_internal.h). Private IDF header, pinned IDF 5.5; the SDK uses it too.
#include <esp_private/esp_sleep_internal.h>
#if CROSSPOINT_BENCH_CONSOLE
#include <esp32-hal-periman.h>
#include <soc/ledc_reg.h>
#include <soc/ledc_struct.h>
#endif
#else
#define HAL_FRONTLIGHT_SLEEP_CLOCK 0
#endif

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  // Release the pad holds HalPowerManager::startDeepSleep() armed. Unconditional:
  // the hold survives the wake reset, and a held pad silently ignores
  // ledcAttach() (the light would stay dark until a power-cycle).
  for (const int8_t pin : {BoardConfig::ACTIVE.frontlight.gpio, BoardConfig::ACTIVE.frontlight.gpioWarm}) {
    if (pin >= 0) gpio_hold_dis(static_cast<gpio_num_t>(pin));
  }
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  if (BoardConfig::isX4Pro()) {
    // A deep-sleep wake keeps the RTC-memory request count (see above) while
    // sleepClockHeld starts false. This HAL is the only XTAL-mode user on this
    // image (Arduino LEDC channels take none; the IDF LEDC driver only for
    // KEEP_ALIVE channels, unused here), so start from zero.
    esp_sleep_sub_mode_force_disable(ESP_SLEEP_DIG_USE_XTAL_MODE);
    sleepClockHeld = false;
  }
#endif
  if (!manager.present()) return;

  manager.begin();
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  sleepCapable = BoardConfig::isX4Pro() && ledcGetClockSource() == LEDC_USE_XTAL_CLK;
  if (sleepCapable) {
    // The pads keep their LEDC drive in light sleep rather than a sleep config.
    for (const int8_t pin : {BoardConfig::ACTIVE.frontlight.gpio, BoardConfig::ACTIVE.frontlight.gpioWarm}) {
      if (pin >= 0) gpio_sleep_sel_dis(static_cast<gpio_num_t>(pin));
    }
  }
#endif
  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
  applyBrightness();
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s naps=%d", lastBrightness, manager.colorTemperature(),
          lit ? "on" : "off", sleepCapable ? 1 : 0);
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) applyBrightness();
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (on == lit) return;
  lit = on;
  applyBrightness();
}

void HalFrontlight::applyBrightness() {
  const uint8_t percent = lit ? lastBrightness : 0;
  if (percent != 0) holdSleepClock(true);
  manager.setBrightness(percent);
  if (percent == 0) holdSleepClock(false);
}

void HalFrontlight::holdSleepClock(const bool hold) {
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  if (!sleepCapable || hold == sleepClockHeld) return;
  esp_sleep_sub_mode_config(ESP_SLEEP_DIG_USE_XTAL_MODE, hold);
  sleepClockHeld = hold;
#else
  (void)hold;
#endif
}

int32_t HalFrontlight::sleepClockRequests() const {
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  return esp_sleep_sub_mode_dump_config(nullptr)[ESP_SLEEP_DIG_USE_XTAL_MODE];
#else
  return -1;
#endif
}

#if CROSSPOINT_BENCH_CONSOLE
void HalFrontlight::napProbeArm() {
  probeChannel = -1;
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  if (!sleepClockHeld) return;
  // Probe a channel with a nonzero duty (at 1 % one channel is at 0).
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  const int8_t pin = ledcRead(fl.gpio) != 0 ? fl.gpio : fl.gpioWarm;
  if (pin < 0) return;
  const auto* bus = static_cast<const ledc_channel_handle_t*>(perimanGetPinBus(pin, ESP32_BUS_TYPE_LEDC));
  if (bus == nullptr || bus->channel >= SOC_LEDC_CHANNEL_NUM) return;
  auto& conf0 = LEDC.channel_group[0].channel[bus->channel].conf0;
  if (!conf0.ovf_cnt_en || conf0.ovf_num != 1023) {
    // Counter only; its interrupt stays disabled. The update flag latches at
    // the next period end with the duty unchanged, as every ledcWrite() does.
    conf0.ovf_num = 1023;
    conf0.ovf_cnt_en = 1;
    conf0.low_speed_update = 1;
  }
  conf0.ovf_cnt_rst = 1;
  LEDC.int_clr.val = 1UL << (LEDC_OVF_CNT_LSCH0_INT_CLR_S + bus->channel);
  probeChannel = static_cast<int8_t>(bus->channel);
#endif
}

void HalFrontlight::napProbeCheck(const int64_t sleptUs) {
#if HAL_FRONTLIGHT_SLEEP_CLOCK
  if (probeChannel < 0 || sleptUs < NAP_PROBE_MIN_US) return;
  ++probeChecked;
  if (LEDC.int_raw.val & (1UL << (LEDC_OVF_CNT_LSCH0_INT_RAW_S + probeChannel))) ++probeRan;
#else
  (void)sleptUs;
#endif
}
#endif
