#include "FakeLedc.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_private/esp_sleep_internal.h>

FakeLedc fakeLedc;

bool ledcAttach(const uint8_t pin, const uint32_t freq, const uint8_t resolution) {
  fakeLedc.attachFreq[pin] = freq;
  fakeLedc.attachBits[pin] = resolution;
  return true;
}

bool ledcWrite(const uint8_t pin, const uint32_t duty) {
  fakeLedc.duty[pin] = duty;
  fakeLedc.events.push_back({FakeEvent::Write, pin, duty});
  return true;
}

ledc_clk_cfg_t ledcGetClockSource() { return static_cast<ledc_clk_cfg_t>(fakeLedc.clockSource); }

esp_err_t gpio_hold_dis(gpio_num_t) { return 0; }

esp_err_t gpio_sleep_sel_dis(const gpio_num_t gpio) {
  fakeLedc.sleepSelDisabled[gpio] = true;
  return 0;
}

esp_err_t esp_sleep_sub_mode_config(const esp_sleep_sub_mode_t mode, const bool activate) {
  if (mode != ESP_SLEEP_DIG_USE_XTAL_MODE) return 0;
  fakeLedc.xtalRefs += activate ? 1 : -1;
  fakeLedc.events.push_back({activate ? FakeEvent::XtalOn : FakeEvent::XtalOff, -1, 0});
  return 0;
}

esp_err_t esp_sleep_sub_mode_force_disable(const esp_sleep_sub_mode_t mode) {
  if (mode != ESP_SLEEP_DIG_USE_XTAL_MODE) return 0;
  ++fakeLedc.xtalForceDisables;
  fakeLedc.xtalRefs = 0;
  return 0;
}

int32_t* esp_sleep_sub_mode_dump_config(FILE*) {
  static int32_t counts[ESP_SLEEP_MODE_MAX];
  counts[ESP_SLEEP_DIG_USE_XTAL_MODE] = fakeLedc.xtalRefs;
  return counts;
}
