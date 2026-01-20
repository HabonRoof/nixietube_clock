#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "message_types.h"
#include "nixie_driver.h"
#include "led_driver.h"

enum class LedEffectType
{
    NONE,
    BREATH,
    RAINBOW
};

class DisplayDaemon
{
public:
    DisplayDaemon(INixieDriver &nixie_driver, ILedDriver &led_driver);
    ~DisplayDaemon();

    void start();
    QueueHandle_t get_queue() const;

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const DisplayMessage &msg);
    void update_effects(uint32_t dt_ms);

    // Effect implementations
    void run_breath_effect(uint32_t dt_ms);
    void run_rainbow_effect(uint32_t dt_ms);
    void apply_backlight_to_all(const BackLightState &state);

    INixieDriver &nixie_driver_;
    ILedDriver &led_driver_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;

    // State
    DisplayMode current_mode_;
    uint32_t manual_number_;
    LedEffectType current_effect_type_;
    
    // Effect parameters
    float effect_color_phase_;
    float effect_speed_;
    uint8_t effect_max_brightness_;
    BackLightState base_backlight_;
};