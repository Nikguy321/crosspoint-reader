#pragma once

#include <driver/gpio.h>

typedef enum {
  ESP_SLEEP_DIG_USE_RC_FAST_MODE,
  ESP_SLEEP_DIG_USE_XTAL_MODE,
  ESP_SLEEP_MODE_MAX,
} esp_sleep_sub_mode_t;

#include <cstdint>
#include <cstdio>

esp_err_t esp_sleep_sub_mode_config(esp_sleep_sub_mode_t mode, bool activate);
esp_err_t esp_sleep_sub_mode_force_disable(esp_sleep_sub_mode_t mode);
int32_t* esp_sleep_sub_mode_dump_config(FILE* stream);
