#include "led_back_light.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oklch_color.h"
#include "sdkconfig.h"
#include "ws2812.h"

namespace LedBackLight
{
namespace
{
constexpr gpio_num_t kOnboardLedPin = static_cast<gpio_num_t>(39);
constexpr TickType_t kFrameDelayTicks = pdMS_TO_TICKS(10);
constexpr uint8_t kDefaultChroma = 255;
constexpr uint8_t kDefaultBrightness = 200;
constexpr uint8_t kHueIncrement = 1;

const char *kAppLogTag = "led_back_light";

class Controller
{
public:
    Controller()
        : led_strip_(kOnboardLedPin),
          hue_value_(0)
    {
        color_generator_.set_d65_defaults();
    }

    void run()
    {
        ESP_LOGI(kAppLogTag, "Starting LED backlight controller");
        while (true)
        {
            render_frame();
            advance_hue();
            vTaskDelay(kFrameDelayTicks);
        }
    }

private:
    void render_frame()
    {
        LightColor linear_color = color_generator_.convert(hue_value_, kDefaultChroma, kDefaultBrightness);
        LightColor corrected_color = color_generator_.apply_gamma(linear_color);

        for (std::size_t group_index = 0; group_index < Ws2812Strip::kGroupCount; ++group_index)
        {
            esp_err_t status = led_strip_.set_group(group_index, corrected_color.red, corrected_color.green, corrected_color.blue);
            if (status != ESP_OK)
            {
                ESP_LOGE(kAppLogTag, "set_group failed (group=%zu, err=%d)", group_index, status);
                return;
            }
        }

        esp_err_t transmit_status = led_strip_.show();
        if (transmit_status != ESP_OK)
        {
            ESP_LOGE(kAppLogTag, "show failed: %d", transmit_status);
        }
    }

    void advance_hue()
    {
        hue_value_ = static_cast<uint8_t>(hue_value_ + kHueIncrement);
    }

    Ws2812Strip led_strip_;
    OklchColorGenerator color_generator_;
    uint8_t hue_value_;
};
} // namespace

void run()
{
    Controller controller;
    controller.run();
}

} // namespace LedBackLight
