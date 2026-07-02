#pragma once

#include "driver/gpio.h"

// Active-low tact switches with 10k pull-up to +3V3.
// BTN_0: alarm stop / divergence re-trigger
// BTN_1: display mode cycle
// BTN_2: backlight profile cycle
constexpr gpio_num_t kButtonPins[] = {
    GPIO_NUM_8,
    GPIO_NUM_12,
    GPIO_NUM_13,
};
constexpr uint8_t kButtonCount = 3;

constexpr uint8_t kButtonAlarmStop = 0;
constexpr uint8_t kButtonModeCycle = 1;
constexpr uint8_t kButtonProfileCycle = 2;
