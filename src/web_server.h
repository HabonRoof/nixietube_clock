#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_store.h"
#include <ctime>

class SystemController;

class WebServer
{
public:
    WebServer(SystemController &system_controller, SettingsStore &store);
    ~WebServer();

    void start();
    void stop();

    bool load_settings(ClockSettings *out_settings);
    bool apply_settings(const ClockSettings &settings, const struct tm *new_time);

private:
    static void task_entry(void *param);
    void run();

    bool start_ap();
    bool start_http();
    void stop_http();

    SystemController &system_controller_;
    SettingsStore &store_;
    TaskHandle_t task_handle_;
};
