#include "ws2812.h"

#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <vector>

namespace
{
constexpr uint32_t kRmtResolutionHz = 40000000; // 25ns resolution
constexpr uint32_t kTicksPerMicrosecond = kRmtResolutionHz / 1000000;

constexpr uint32_t kT0hTicks = (kTicksPerMicrosecond * 400) / 1000;
constexpr uint32_t kT0lTicks = (kTicksPerMicrosecond * 850) / 1000;
constexpr uint32_t kT1hTicks = (kTicksPerMicrosecond * 800) / 1000;
constexpr uint32_t kT1lTicks = (kTicksPerMicrosecond * 450) / 1000;

constexpr std::size_t kBytesPerPixel = 3;
constexpr std::size_t kItemsPerLed = 24;

const char *kTag = "ws2812";
} // namespace

Ws2812Strip::Ws2812Strip(gpio_num_t data_pin, std::size_t led_count)
    : data_pin_(data_pin),
      led_count_(led_count),
      tx_channel_(nullptr),
      copy_encoder_(nullptr),
      pixel_buffer_()
{
    if (led_count_ == 0)
    {
#ifdef CONFIG_WS2812_LED_COUNT
        led_count_ = CONFIG_WS2812_LED_COUNT;
#else
        led_count_ = kTotalLedCount;
#endif
    }
    pixel_buffer_.assign(led_count_ * kBytesPerPixel, 0);
    configure_pad(data_pin_);
    if (configure_rmt(data_pin_) == ESP_OK)
    {
        ESP_LOGI(kTag, "WS2812 initialized on GPIO%d (total led_count=%zu)", static_cast<int>(data_pin_), led_count_);
    }
}

Ws2812Strip::~Ws2812Strip()
{
    if (copy_encoder_)
    {
        rmt_del_encoder(copy_encoder_);
        copy_encoder_ = nullptr;
    }
    if (tx_channel_)
    {
        rmt_disable(tx_channel_);
        rmt_del_channel(tx_channel_);
        tx_channel_ = nullptr;
    }
}

std::size_t Ws2812Strip::get_led_count() const
{
    return led_count_;
}

esp_err_t Ws2812Strip::set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    if (index >= led_count_ || pixel_buffer_.empty())
    {
        return ESP_ERR_INVALID_ARG;
    }

    std::size_t offset = index * kBytesPerPixel;
    pixel_buffer_[offset + 0] = red;
    pixel_buffer_[offset + 1] = green;
    pixel_buffer_[offset + 2] = blue;
    return ESP_OK;
}

esp_err_t Ws2812Strip::set_group(std::size_t group_index, uint8_t red, uint8_t green, uint8_t blue)
{
    std::size_t first_led = group_index * kGroupSize;
    if (first_led >= led_count_ || first_led + kGroupSize > led_count_)
    {
        return ESP_ERR_INVALID_ARG;
    }
    for (std::size_t led = 0; led < kGroupSize; ++led)
    {
        esp_err_t status = set_pixel(first_led + led, red, green, blue);
        if (status != ESP_OK)
        {
            return status;
        }
    }
    return ESP_OK;
}

esp_err_t Ws2812Strip::fill(uint8_t red, uint8_t green, uint8_t blue)
{
    if (pixel_buffer_.empty())
    {
        return ESP_ERR_INVALID_STATE;
    }
    for (std::size_t index = 0; index < led_count_; ++index)
    {
        std::size_t offset = index * kBytesPerPixel;
        pixel_buffer_[offset + 0] = red;
        pixel_buffer_[offset + 1] = green;
        pixel_buffer_[offset + 2] = blue;
    }
    return ESP_OK;
}

esp_err_t Ws2812Strip::show()
{
    if (!tx_channel_ || !copy_encoder_ || pixel_buffer_.empty())
    {
        return ESP_ERR_INVALID_STATE;
    }

    std::vector<rmt_symbol_word_t> symbols(led_count_ * kItemsPerLed);
    for (std::size_t led = 0; led < led_count_; ++led)
    {
        uint8_t red = pixel_buffer_[led * kBytesPerPixel + 0];
        uint8_t green = pixel_buffer_[led * kBytesPerPixel + 1];
        uint8_t blue = pixel_buffer_[led * kBytesPerPixel + 2];
        uint8_t grb_bytes[3] = {green, red, blue};
        for (std::size_t byte_index = 0; byte_index < 3; ++byte_index)
        {
            build_symbols_for_byte(&symbols[led * kItemsPerLed + byte_index * 8], grb_bytes[byte_index]);
        }
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
        .flags = {
            .eot_level = 0,
            .queue_nonblocking = 0,
        },
    };
    esp_err_t status = rmt_transmit(tx_channel_, copy_encoder_, symbols.data(),
                                    symbols.size() * sizeof(rmt_symbol_word_t), &transmit_config);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_transmit failed: %d", status);
        return status;
    }

    status = rmt_tx_wait_all_done(tx_channel_, pdMS_TO_TICKS(100));
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_tx_wait_all_done failed: %d", status);
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

void Ws2812Strip::configure_pad(gpio_num_t data_pin) const
{
    if (GPIO_IS_VALID_GPIO(data_pin))
    {
        esp_rom_gpio_pad_select_gpio(data_pin);
        gpio_reset_pin(data_pin);
    }
}

esp_err_t Ws2812Strip::configure_rmt(gpio_num_t data_pin)
{
    rmt_tx_channel_config_t config = {
        .gpio_num = data_pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = kRmtResolutionHz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {
            .invert_out = 0,
            .with_dma = 0,
            .io_loop_back = 0,
            .io_od_mode = 0,
            .allow_pd = 0,
        },
    };
    esp_err_t status = rmt_new_tx_channel(&config, &tx_channel_);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_new_tx_channel failed: %d", status);
        return status;
    }

    rmt_copy_encoder_config_t encoder_config = {};
    status = rmt_new_copy_encoder(&encoder_config, &copy_encoder_);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_new_copy_encoder failed: %d", status);
        rmt_del_channel(tx_channel_);
        tx_channel_ = nullptr;
        return status;
    }

    status = rmt_enable(tx_channel_);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_enable failed: %d", status);
        rmt_del_encoder(copy_encoder_);
        copy_encoder_ = nullptr;
        rmt_del_channel(tx_channel_);
        tx_channel_ = nullptr;
        return status;
    }
    return ESP_OK;
}

void Ws2812Strip::build_symbols_for_byte(rmt_symbol_word_t *symbols, uint8_t byte_value) const
{
    for (int bit = 0; bit < 8; ++bit)
    {
        bool is_one = (byte_value & (1 << (7 - bit))) != 0;
        if (is_one)
        {
            symbols[bit].level0 = 1;
            symbols[bit].duration0 = kT1hTicks;
            symbols[bit].level1 = 0;
            symbols[bit].duration1 = kT1lTicks;
        }
        else
        {
            symbols[bit].level0 = 1;
            symbols[bit].duration0 = kT0hTicks;
            symbols[bit].level1 = 0;
            symbols[bit].duration1 = kT0lTicks;
        }
    }
}
