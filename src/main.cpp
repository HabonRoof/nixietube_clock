#include "ws2812.h"
#include "led_setup.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *kLogTag = "main";
static constexpr gpio_num_t kLedDataInPin = static_cast<gpio_num_t>(39);

extern "C" void app_main(void)
{
    ESP_LOGI(kLogTag, "Initializing Nixie Clock backlight...");

    static Ws2812Strip led_strip(kLedDataInPin);

    BackLightState default_backlight = make_default_backlight(128);
    LedControlHandles led_handles{};
    initialize_led_module(led_strip, default_backlight, led_handles);
    (void)led_handles; // for future WebUI/App wiring
}
