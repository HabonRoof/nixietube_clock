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
#include "daemons/cli_daemon.h"
#include "settings_store.h"
#include "web_server.h"
#include "nvs_flash.h"

static const char *kLogTag = "main";

// Pin Definitions
// Pins are now managed in SystemController::init_hardware()

extern "C" void app_main(void)
{
    ESP_LOGI(kLogTag, "Starting Nixie Clock System...");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    // 0. Initialize Hardware (I2C, UART, GPIO, RMT)
    HardwareHandles hw_handles = SystemController::init_hardware();

    // 1. Initialize Hardware Drivers
    
    // Initialize LED Strip Driver
    static LedDriver led_driver(hw_handles.led_rmt_channel, hw_handles.led_rmt_encoder);
    
    // Initialize Nixie Driver
    // Now NixieDriver manages its own tubes internally.
    static NixieDriver nixie_driver;
    nixie_driver.nixie_scan_start(hw_handles.i2c_port);
    
    // Initialize Audio Driver
    static AudioDriver audio_driver(hw_handles.audio_uart_port);

    // 2. Initialize Daemons
    static DisplayDaemon display_daemon(nixie_driver, led_driver);
    static AudioDaemon audio_daemon(audio_driver);

    // 3. Initialize System Controller
    static SystemController system_controller(display_daemon, audio_daemon);

    // 3.1 Load persisted settings and apply
    static SettingsStore settings_store;
    ClockSettings settings;
    if (settings_store.load(&settings)) {
        system_controller.apply_settings(settings, nullptr);
    }

    // 4. Initialize CLI Daemon
    static CliDaemon cli_daemon(system_controller);

    // 4.1 Initialize Web Server
    static WebServer web_server(system_controller, settings_store);

    // 5. Start Tasks
    ESP_LOGI(kLogTag, "Starting Daemons...");
    display_daemon.start();
    audio_daemon.start();
    system_controller.start();
    cli_daemon.start();
    web_server.start();

    ESP_LOGI(kLogTag, "System Running.");
    
    // Main task can now delete itself or monitor stack usage
    vTaskDelete(nullptr);
}
