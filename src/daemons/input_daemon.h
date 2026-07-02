#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "button_config.h"

class SystemController;

class InputDaemon
{
public:
    explicit InputDaemon(SystemController &system_controller);
    ~InputDaemon();

    void start();

private:
    struct DebounceState {
        bool stable_pressed;
        bool last_raw_pressed;
        uint32_t change_ms;
        uint32_t last_fire_ms;
    };

    static void task_entry(void *param);
    void loop();
    void init_gpio();
    bool process_debounce(uint8_t button_id, bool raw_pressed, uint32_t now_ms);
    void post_button_pressed(uint8_t button_id);
    void poll_als();

    SystemController &system_controller_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;
    DebounceState debounce_[kButtonCount];
    uint32_t tick_ms_;
};
