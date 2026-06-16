#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "nixie_driver.h"
#include "led_driver.h"
#include "audio_driver.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "daemons/gasgauge_daemon.h"
#include "charger_controller.h"
#include "bq27441/bq27441.h"
#include "power_switch/gpio_power_switch.h"
#include "bq25601/bq25601.h"
#include "power_controller.h"
#include "system_controller.h"
#include "system_state.h"
#include "daemons/cli_daemon.h"
#include "settings_store.h"
#include "web_server.h"
#include "nvs_flash.h"
#include "i2c_debug_config.h"

static const char *kLogTag = "main";

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(kLogTag, "Starting Nixie Clock System...");

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(nvs_err);
    }

    HardwareHandles hw_handles = SystemController::init_hardware();

    static Bq27441 gasgauge_driver(hw_handles.i2c_port);
    static SystemState system_state;

    static LedDriver led_driver(hw_handles.led_rmt_channel, hw_handles.led_rmt_encoder);
    static NixieDriver nixie_driver;

    static AudioDriver audio_driver(hw_handles.audio_uart_port);
    static GpioPowerSwitch power_switch_driver;
    static Bq25601 charger_driver(hw_handles.i2c_port);

    static DisplayDaemon display_daemon(nixie_driver, led_driver, system_state);
    static PowerController power_controller(power_switch_driver);
    static AudioDaemon audio_daemon(audio_driver, power_controller);
    static SystemController system_controller(display_daemon, audio_daemon);
    static GasgaugeDaemon gasgauge_daemon(gasgauge_driver, system_state);
    static ChargerController charger_controller(charger_driver);

    static SettingsStore settings_store;
    ClockSettings settings;
    if (settings_store.load(&settings)) {
        system_controller.apply_settings(settings, nullptr);
    }

    static CliDaemon cli_daemon(system_controller, charger_controller, gasgauge_daemon, power_controller);
    static WebServer web_server(system_controller, settings_store);

    ESP_LOGI(kLogTag, "Starting Daemons...");
    power_controller.init();
    power_controller.set_hv_enabled(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!i2c_debug::kDisablePca9685I2c) {
        nixie_driver.nixie_scan_start(hw_handles.i2c_port);
    } else {
        ESP_LOGW(kLogTag, "PCA9685 I2C disabled (nixie scan not started)");
    }

    display_daemon.start();
    audio_daemon.start();
    system_controller.start();
    gasgauge_daemon.start();
    vTaskDelay(pdMS_TO_TICKS(100));
    charger_controller.init();
    vTaskDelay(pdMS_TO_TICKS(100));
    cli_daemon.start();
    vTaskDelay(pdMS_TO_TICKS(100));
    web_server.start();
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(kLogTag, "System Running.");

    vTaskDelete(nullptr);
}
