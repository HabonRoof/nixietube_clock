#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings_json.h"
#include "system_state.h"
#include <ctime>

class SystemController;
class AudioDaemon;
class WifiManager;

class WebServer
{
public:
    WebServer(SystemController &system_controller, SystemState &system_state, AudioDaemon &audio_daemon,
              WifiManager &wifi_manager);
    ~WebServer();

    void start();
    void stop();

    bool load_settings(ClockSettings *out_settings);
    bool apply_settings(const ClockSettings &settings, const struct tm *new_time);
    bool apply_settings_update(const ParsedSettingsUpdate &update);
    bool get_time_status(struct tm *local_out, bool *time_valid, bool *osf, float *temperature,
                         time_t *unix_utc = nullptr);

    AudioDaemon &audio_daemon();
    WifiManager &wifi_manager();

private:
    static void task_entry(void *param);
    void run();

    bool start_http();
    void stop_http();

    SystemController &system_controller_;
    SystemState &system_state_;
    AudioDaemon &audio_daemon_;
    WifiManager &wifi_manager_;
    TaskHandle_t task_handle_;
    bool http_running_;
};
