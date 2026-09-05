#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "nixie_driver.h"
#include "led_driver.h"
#include "audio_driver.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "gasgauge_service.h"
#include "charger_controller.h"
#include "bq27441/bq27441.h"
#include "power_switch/gpio_power_switch.h"
#include "bq25601/bq25601.h"
#include "power_controller.h"
#include "system_controller.h"
#include "system_state.h"
#include "daemons/cli_daemon.h"
#include "daemons/input_daemon.h"
#include "web_server.h"
#include "nvs_flash.h"
#include "i2c_debug_config.h"
#include "driver/i2c.h"
#include "ltr303/ltr303.h"
#include "daemons/als_daemon.h"

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
    
    // Driver injection points
    static Bq27441 gasgauge_driver(hw_handles.i2c0_port);
    static GasgaugeService gasgauge_service(gasgauge_driver);
    static SystemState system_state;

    static LedDriver led_driver(hw_handles.led_rmt_channel, hw_handles.led_rmt_encoder);
    static NixieDriver nixie_driver;

    static AudioDriver audio_driver(hw_handles.audio_uart_port);
    static GpioPowerSwitch power_switch_driver;
    static Bq25601 charger_driver(hw_handles.i2c0_port);

    bool gasgauge_ready = false;
    if (!i2c_debug::kDisableGasgauge) {
        GasgaugeDeviceInfo info;
        if (gasgauge_service.probe_device_info(info) && gasgauge_service.is_ready()) {
            if (gasgauge_service.configure_capacity(GasgaugeService::kDefaultCapacityMah, false)) {
                gasgauge_ready = true;
                ESP_LOGI(kLogTag, "Gasgauge probed and capacity configured");
            } else {
                ESP_LOGW(kLogTag, "Gasgauge probe OK but capacity configure failed");
            }
        } else {
            ESP_LOGW(kLogTag, "Gasgauge probe failed at boot; SystemController will retry");
        }
    } else {
        ESP_LOGW(kLogTag, "Gasgauge disabled");
    }

    static DisplayDaemon display_daemon(nixie_driver, led_driver, system_state);
    static PowerController power_controller(power_switch_driver);

    system_state.load();
    ClockSettings boot_settings = SystemState::defaults();
    system_state.get_settings(&boot_settings);

    static AudioDaemon audio_daemon(audio_driver, power_controller, boot_settings.volume);
    static ChargerController charger_controller(charger_driver);
    static SystemController system_controller(display_daemon, audio_daemon, system_state,
                                              i2c_debug::kDisableGasgauge ? nullptr : &gasgauge_service,
                                              gasgauge_ready,
                                              &charger_controller);
    display_daemon.set_system_queue(system_controller.get_queue());
    static InputDaemon input_daemon(system_controller);

    ClockSettings settings;
    system_state.get_settings(&settings);
    system_controller.apply_settings(settings, nullptr);

    static CliDaemon cli_daemon(system_controller, charger_controller, gasgauge_service,
                                power_controller, system_state, audio_daemon);
    static WebServer web_server(system_controller, system_state, audio_daemon);

    static Ltr303 *ltr303 = nullptr;
    static AlsDaemon *als_daemon = nullptr;
    if (!i2c_debug::kDisableALS && hw_handles.i2c1_port < I2C_NUM_MAX) {
        static Ltr303 ltr303_instance(hw_handles.i2c1_port);
        ltr303 = &ltr303_instance;
        static AlsDaemon als_daemon_instance(*ltr303, display_daemon, nixie_driver, system_state);
        als_daemon = &als_daemon_instance;
    }

    ESP_LOGI(kLogTag, "Starting Daemons...");
    power_controller.init();
    power_controller.set_hv_enabled(true);
    power_controller.set_dfplayer_enabled(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!i2c_debug::kDisablePca9685I2c) {
        nixie_driver.nixie_scan_start(hw_handles.i2c0_port);
    } else {
        ESP_LOGW(kLogTag, "PCA9685 I2C disabled (nixie scan not started)");
    }

    display_daemon.start();
    audio_daemon.start();
    system_controller.start();
    input_daemon.start();
    if (ltr303 != nullptr && als_daemon != nullptr) {
        if (ltr303->init()) {
            als_daemon->start();
        } else {
            ESP_LOGW(kLogTag, "LTR-303 probe failed; ALS daemon not started");
        }
    } else if (i2c_debug::kDisableALS) {
        ESP_LOGW(kLogTag, "ALS disabled");
    }
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
