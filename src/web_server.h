#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_state.h"
#include <ctime>

class SystemController;

class WebServer
{
public:
    WebServer(SystemController &system_controller, SystemState &system_state);
    ~WebServer();

    void start();
    void stop();

    bool load_settings(ClockSettings *out_settings);
    bool apply_settings(const ClockSettings &settings, const struct tm *new_time);
    bool get_time_status(struct tm *local_out, bool *time_valid, bool *osf, float *temperature,
                         time_t *unix_utc = nullptr);

private:
    static void task_entry(void *param);
    void run();

    bool start_ap();
    bool start_http();
    void stop_http();

    SystemController &system_controller_;
    SystemState &system_state_;
    TaskHandle_t task_handle_;
};
