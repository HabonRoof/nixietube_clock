#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_state.h"
#include <ctime>

class SystemController;
class AudioDaemon;

class WebServer
{
public:
    WebServer(SystemController &system_controller, SystemState &system_state, AudioDaemon &audio_daemon);
    ~WebServer();

    void start();
    void stop();

    bool load_settings(ClockSettings *out_settings);
    bool apply_settings(const ClockSettings &settings, const struct tm *new_time);
    void preview_profile(const BacklightProfile &profile);
    void preview_protection_brightness(uint8_t nixie_brightness);
    bool get_time_status(struct tm *local_out, bool *time_valid, bool *osf, float *temperature,
                         time_t *unix_utc = nullptr);

    AudioDaemon &audio_daemon();

private:
    static void task_entry(void *param);
    void run();

    bool start_ap();
    bool start_http();
    void stop_http();

    SystemController &system_controller_;
    SystemState &system_state_;
    AudioDaemon &audio_daemon_;
    TaskHandle_t task_handle_;
};
