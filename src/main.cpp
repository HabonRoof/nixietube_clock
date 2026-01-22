#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "nixie_driver.h"
#include "led_driver.h"
#include "audio_driver.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "system_controller.h"

static const char *kLogTag = "main";

// Pin Definitions
static constexpr gpio_num_t kLedDataInPin = static_cast<gpio_num_t>(11);
static constexpr gpio_num_t kDfPlayerRxPin = static_cast<gpio_num_t>(17);
static constexpr gpio_num_t kDfPlayerTxPin = static_cast<gpio_num_t>(18);
static constexpr uart_port_t kDfPlayerUart = UART_NUM_1;

extern "C" void app_main(void)
{
    ESP_LOGI(kLogTag, "Starting Nixie Clock System...");

    // 1. Initialize Hardware Drivers
    
    // Initialize LED Strip Driver
    static LedDriver led_driver(kLedDataInPin);
    
    // Initialize Nixie Driver
    // Now NixieDriver manages its own tubes internally.
    static NixieDriver nixie_driver; 
    nixie_driver.nixie_scan_start();
    
    // Initialize Audio Driver
    static AudioDriver audio_driver(kDfPlayerUart, kDfPlayerTxPin, kDfPlayerRxPin);

    // 2. Initialize Daemons
    static DisplayDaemon display_daemon(nixie_driver, led_driver);
    static AudioDaemon audio_daemon(audio_driver);

    // 3. Initialize System Controller
    static SystemController system_controller(display_daemon, audio_daemon);

    // 4. Start Tasks
    ESP_LOGI(kLogTag, "Starting Daemons...");
    display_daemon.start();
    audio_daemon.start();
    system_controller.start();

    ESP_LOGI(kLogTag, "System Running.");
    
    // Main task can now delete itself or monitor stack usage
    vTaskDelete(nullptr);
}
