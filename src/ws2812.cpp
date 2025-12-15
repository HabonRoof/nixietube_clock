#include "ws2812.h"

#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <vector>

namespace
{
constexpr rmt_channel_t kRmtChannel = RMT_CHANNEL_0;
constexpr uint32_t kRmtClockDivider = 2;
constexpr uint32_t kApbClockHz = 80000000;
constexpr uint32_t kTicksPerMicrosecond = kApbClockHz / 1000000 / kRmtClockDivider;

constexpr uint32_t kT0hTicks = (kTicksPerMicrosecond * 400) / 1000;
constexpr uint32_t kT0lTicks = (kTicksPerMicrosecond * 850) / 1000;
constexpr uint32_t kT1hTicks = (kTicksPerMicrosecond * 800) / 1000;
constexpr uint32_t kT1lTicks = (kTicksPerMicrosecond * 450) / 1000;

constexpr std::size_t kBytesPerPixel = 3;
constexpr std::size_t kItemsPerLed = 24;

const char *kTag = "ws2812";
} // namespace

Ws2812Strip::Ws2812Strip(gpio_num_t output_pin, std::size_t led_count)
    : output_pin_(output_pin),
      led_count_(led_count),
      rmt_driver_ready_(false),
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
    configure_pad(output_pin_);
    if (configure_rmt(output_pin_) == ESP_OK)
    {
        rmt_driver_ready_ = true;
        ESP_LOGI(kTag, "WS2812 initialized on GPIO%d (led_count=%zu)", static_cast<int>(output_pin_), led_count_);
    }
}

Ws2812Strip::~Ws2812Strip()
{
    if (rmt_driver_ready_)
    {
        rmt_driver_uninstall(kRmtChannel);
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
    if (!rmt_driver_ready_ || pixel_buffer_.empty())
    {
        return ESP_ERR_INVALID_STATE;
    }

    std::vector<rmt_item32_t> items(led_count_ * kItemsPerLed);
    for (std::size_t led = 0; led < led_count_; ++led)
    {
        uint8_t red = pixel_buffer_[led * kBytesPerPixel + 0];
        uint8_t green = pixel_buffer_[led * kBytesPerPixel + 1];
        uint8_t blue = pixel_buffer_[led * kBytesPerPixel + 2];
        uint8_t grb_bytes[3] = {green, red, blue};
        for (std::size_t byte_index = 0; byte_index < 3; ++byte_index)
        {
            build_rmt_items_for_byte(&items[led * kItemsPerLed + byte_index * 8], grb_bytes[byte_index]);
        }
    }

    esp_err_t status = rmt_write_items(kRmtChannel, items.data(), items.size(), true);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_write_items failed: %d", status);
        return status;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

void Ws2812Strip::configure_pad(gpio_num_t output_pin) const
{
    if (GPIO_IS_VALID_GPIO(output_pin))
    {
        esp_rom_gpio_pad_select_gpio(output_pin);
        gpio_reset_pin(output_pin);
    }
}

esp_err_t Ws2812Strip::configure_rmt(gpio_num_t output_pin)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(output_pin, kRmtChannel);
    config.clk_div = kRmtClockDivider;
    esp_err_t status = rmt_config(&config);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_config failed: %d", status);
        return status;
    }
    status = rmt_driver_install(config.channel, 0, 0);
    if (status != ESP_OK)
    {
        ESP_LOGE(kTag, "rmt_driver_install failed: %d", status);
        return status;
    }
    return ESP_OK;
}

void Ws2812Strip::build_rmt_items_for_byte(rmt_item32_t *items, uint8_t byte_value) const
{
    for (int bit = 0; bit < 8; ++bit)
    {
        bool is_one = (byte_value & (1 << (7 - bit))) != 0;
        if (is_one)
        {
            items[bit].level0 = 1;
            items[bit].duration0 = kT1hTicks;
            items[bit].level1 = 0;
            items[bit].duration1 = kT1lTicks;
        }
        else
        {
            items[bit].level0 = 1;
            items[bit].duration0 = kT0hTicks;
            items[bit].level1 = 0;
            items[bit].duration1 = kT0lTicks;
        }
    }
}
