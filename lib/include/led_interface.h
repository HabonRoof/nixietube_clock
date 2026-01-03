#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

class ILedStrip
{
public:
    virtual ~ILedStrip() = default;

    virtual std::size_t get_led_count() const = 0;
    virtual esp_err_t set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue) = 0;
    virtual esp_err_t show() = 0;
};
