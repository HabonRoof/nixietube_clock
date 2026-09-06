#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "button_config.h"

class SystemController;
class WifiManager;

class InputDaemon
{
public:
    InputDaemon(SystemController &system_controller, WifiManager *wifi_manager = nullptr);
    ~InputDaemon();

    void start();

private:
    struct DebounceState {
        bool stable_pressed;
        bool last_raw_pressed;
        uint32_t change_ms;
        uint32_t last_fire_ms;
    };

    struct ApConfigComboState {
        bool tracking;
        bool action_fired;
        uint32_t press_start_ms;
    };

    static void task_entry(void *param);
    void loop();
    void init_gpio();
    void post_button_pressed(uint8_t button_id);
    void poll_als();
    bool process_profile_button(uint8_t button_id, bool raw_pressed, bool prev_stable);
    void process_ap_config_combo(bool mode_pressed, bool profile_pressed);
    bool ap_combo_blocks_button(uint8_t button_id) const;
    void clear_ap_combo_if_released();

    SystemController &system_controller_;
    WifiManager *wifi_manager_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;
    DebounceState debounce_[kButtonCount];
    ApConfigComboState ap_config_combo_;
    uint32_t tick_ms_;

    static constexpr uint32_t kLongPressMs = 3000;
};
