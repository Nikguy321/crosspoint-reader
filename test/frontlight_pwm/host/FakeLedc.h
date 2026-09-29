#pragma once

#include <cstdint>
#include <vector>

// What the frontlight code did to the (fake) LEDC and sleep controller, in order.
struct FakeEvent {
  enum Kind : uint8_t { Write, XtalOn, XtalOff } kind;
  int pin;
  uint32_t duty;
};

struct FakeLedc {
  int clockSource = 0;  // ledc_clk_cfg_t
  // ESP_SLEEP_DIG_USE_XTAL_MODE references. Like the IDF's RTC-memory count,
  // nothing but the code under test resets it (a deep-sleep wake keeps it).
  int xtalRefs = 0;
  int xtalForceDisables = 0;
  uint32_t duty[64] = {};
  uint32_t attachFreq[64] = {};
  uint8_t attachBits[64] = {};
  bool sleepSelDisabled[64] = {};
  std::vector<FakeEvent> events;

  void clearLog() { events.clear(); }
};

extern FakeLedc fakeLedc;
