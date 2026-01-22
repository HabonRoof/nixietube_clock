#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "color_model.h"
#include <cstddef>
#include <cstdint>

struct BackLightState
{
    HsvColor color;
    uint8_t brightness;
};

// Abstract Interface for LED Driver
class ILedDriver
{
public:
    virtual ~ILedDriver() = default;

    // Core LED control methods (formerly in ILedStrip)
    virtual std::size_t get_led_count() const = 0;
    virtual esp_err_t set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue) = 0;
    virtual esp_err_t show() = 0;
    
    // Additional helper methods
    virtual esp_err_t fill(uint8_t red, uint8_t green, uint8_t blue) = 0;
    virtual esp_err_t clear() = 0;
};

// Forward declaration
class Ws2812Strip;

// Concrete Implementation wrapping Ws2812Strip
class LedDriver : public ILedDriver
{
public:
    explicit LedDriver(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t copy_encoder);
    ~LedDriver() override;

    std::size_t get_led_count() const override;
    esp_err_t set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue) override;
    esp_err_t show() override;
    esp_err_t fill(uint8_t red, uint8_t green, uint8_t blue) override;
    esp_err_t clear() override;

private:
    Ws2812Strip* strip_;
};