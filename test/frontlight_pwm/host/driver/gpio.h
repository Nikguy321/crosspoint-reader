#pragma once

typedef int gpio_num_t;
typedef int esp_err_t;

esp_err_t gpio_hold_dis(gpio_num_t gpio);
esp_err_t gpio_sleep_sel_dis(gpio_num_t gpio);
