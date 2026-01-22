// WS2812 object-oriented control API
#pragma once

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_err.h"
#include <cstddef>
#include <cstdint>
#include <vector>

class Ws2812Strip
{
public:
    static constexpr std::size_t kTotalLedCount = 24;
    static constexpr std::size_t kGroupSize = 4;
    static constexpr std::size_t kGroupCount = kTotalLedCount / kGroupSize;

    explicit Ws2812Strip(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t copy_encoder, std::size_t led_count = kTotalLedCount);
    ~Ws2812Strip();

    std::size_t get_led_count() const;
    esp_err_t set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t set_group(std::size_t group_index, uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t fill(uint8_t red, uint8_t green, uint8_t blue);
    esp_err_t show();

private:
    void build_symbols_for_byte(rmt_symbol_word_t *symbols, uint8_t byte_value) const;

    std::size_t led_count_;
    rmt_channel_handle_t tx_channel_;
    rmt_encoder_handle_t copy_encoder_;
    std::vector<uint8_t> pixel_buffer_;
};
