#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ltr303/ltr303.h"
#include "daemons/display_daemon.h"
#include "system_state.h"

struct AlsCalSample
{
    uint16_t ch0;
    uint16_t ch1;
    float ratio;
    float lux_raw;
    float lux;
};

class AlsDaemon
{
public:
    AlsDaemon(Ltr303 &sensor, DisplayDaemon &display_daemon, SystemState &system_state);
    ~AlsDaemon();

    void start();
    void apply_stored_sensor_gain();

private:
    struct SavedDisplayState
    {
        DisplayCalSnapshot snapshot;
        bool suppress_auto_brightness;
    };

    static void task_entry(void *param);
    void loop();
    void reset_auto_brightness_state();
    void send_ambient_factor(uint16_t factor);
    void push_display(const DisplayMessage &msg);
    float compensate_ir(float lux, float ratio) const;
    float subtract_self_light(float lux_measured, uint8_t backlight, uint8_t nixie,
                              const AutoBrightnessCalibration &cal) const;
    void update_smoothed_lux(float lux_new, float dt_s);
    void update_calibration_learning(AutoBrightnessCalibration *cal, float lux);
    void clamp_calibration(AutoBrightnessCalibration *cal) const;
    uint16_t lux_to_factor(float lux, const AutoBrightnessCalibration &cal) const;
    uint16_t apply_hysteresis(uint16_t target_factor);
    uint16_t apply_slew_limit(uint16_t target_factor);
    AlsCalSample make_cal_sample(const Ltr303Sample &raw) const;
    bool measure_averaged(AlsCalSample *out, TickType_t period_ticks, TickType_t settle_ticks,
                          int *sample_count_out = nullptr);
    void apply_cal_hardware(uint8_t backlight, uint8_t nixie, bool white_led);
    bool select_calibration_gain(Ltr303Gain *selected_gain);
    void log_cal_sample(const char *label, const AlsCalSample &sample, int sample_count) const;
    bool run_self_calibration();

    Ltr303 &sensor_;
    DisplayDaemon &display_daemon_;
    SystemState &system_state_;
    TaskHandle_t task_handle_;

    float smoothed_lux_;
    bool has_smoothed_lux_;
    bool auto_brightness_active_;
    uint16_t applied_factor_;
    uint16_t pending_factor_;
    uint32_t last_factor_change_ms_;
    uint32_t tick_ms_;
    uint32_t log_counter_;
    uint32_t cal_save_counter_;
    uint8_t last_backlight_;
    uint8_t last_nixie_;

    static constexpr float kGammaDark = 0.6f;
    static constexpr float kTauDarkSec = 2.0f;
    static constexpr float kTauBrightSec = 8.0f;
    static constexpr float kLuxLearnMinAlpha = 0.05f;
    static constexpr float kLuxLearnMaxAlpha = 0.05f;
    static constexpr float kLuxLearnRelaxAlpha = 0.0005f;
    static constexpr uint16_t kHystUp = 24;
    static constexpr uint16_t kHystDown = 16;
    static constexpr uint32_t kMinHoldMs = 4000;
    static constexpr uint16_t kMaxSlewPerStep = 50;
    static constexpr TickType_t kPeriodTicks = pdMS_TO_TICKS(2000);
    static constexpr TickType_t kCalSettleTicks = kPeriodTicks * 2;
    static constexpr TickType_t kCalSamplePeriodTicks = pdMS_TO_TICKS(10000);
    static constexpr uint32_t kLogIntervalTicks = 2;
    static constexpr uint32_t kCalSaveIntervalTicks = 1800;
};
