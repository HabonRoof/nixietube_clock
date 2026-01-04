#include "ws2812.h"
#include "led_setup.h"
#include "audio_setup.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *kLogTag = "main";
static constexpr gpio_num_t kLedDataInPin = static_cast<gpio_num_t>(39);
static constexpr gpio_num_t kDfPlayerRxPin = static_cast<gpio_num_t>(17);
static constexpr gpio_num_t kDfPlayerTxPin = static_cast<gpio_num_t>(18);
static constexpr uart_port_t kDfPlayerUart = UART_NUM_1;

extern "C" void app_main(void)
{
    ESP_LOGI(kLogTag, "Initializing Nixie Clock backlight...");

    static Ws2812Strip led_strip(kLedDataInPin);

    BackLightState default_backlight = make_default_backlight(128);
    LedControlHandles led_handles{};
    initialize_led_module(led_strip, default_backlight, led_handles);
    (void)led_handles; // for future WebUI/App wiring

    ESP_LOGI(kLogTag, "Initializing DFPlayer Mini audio...");
    AudioControlHandles audio_handles{};
    AudioConfig audio_config = make_default_audio_config();
    audio_config.uart_port = kDfPlayerUart;
    audio_config.tx_pin = kDfPlayerTxPin;
    audio_config.rx_pin = kDfPlayerRxPin;
    initialize_audio_module(audio_config, audio_handles);
    (void)audio_handles; // reserved for later control hooks
}
