#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ltr303/ltr303.h"
#include "daemons/display_daemon.h"
#include "nixie_driver.h"
#include "system_state.h"

class AlsDaemon
{
public:
    AlsDaemon(Ltr303 &sensor, DisplayDaemon &display_daemon, INixieDriver &nixie_driver,
              SystemState &system_state);
    ~AlsDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();
    float lux_to_scale(float lux) const;

    Ltr303 &sensor_;
    DisplayDaemon &display_daemon_;
    INixieDriver &nixie_driver_;
    SystemState &system_state_;
    TaskHandle_t task_handle_;

    float smoothed_lux_;
    bool has_smoothed_lux_;
    bool auto_brightness_active_;
    uint32_t log_counter_;

    static constexpr float kEmaAlpha = 0.25f;
    static constexpr float kLuxMin = 1.0f;
    static constexpr float kLuxMax = 500.0f;
    static constexpr float kMinScale = 0.05f;
    static constexpr TickType_t kPeriodTicks = pdMS_TO_TICKS(2000);
    static constexpr uint32_t kLogIntervalTicks = 2; // ~4 s at 0.5 Hz
};
