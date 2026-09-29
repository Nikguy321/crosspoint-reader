#pragma once

// Host stand-in for the Arduino 3.x core pieces the frontlight code uses.
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

#define ESP_ARDUINO_VERSION_MAJOR 3

// Values from the ESP32-S3 soc_module_clk_t (clk_tree_defs.h).
typedef enum {
  LEDC_AUTO_CLK = 0,
  LEDC_USE_APB_CLK = 4,
  LEDC_USE_RC_FAST_CLK = 9,
  LEDC_USE_XTAL_CLK = 11,
} ledc_clk_cfg_t;

bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
ledc_clk_cfg_t ledcGetClockSource();
