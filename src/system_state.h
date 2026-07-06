#pragma once

#include <cstdint>
#include <ctime>
#include "freertos/FreeRTOS.h"

// Bump whenever the layout/meaning of ClockSettings changes. NVS load uses
// this to migrate (rather than discard) older persisted blobs.
constexpr uint16_t kSettingsVersion = 7;
constexpr uint8_t kBacklightProfileCount = 4;

struct BacklightProfile {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t backlight_brightness;
    uint8_t backlight_effect; // 0: static, 1: breath, 2: rainbow, 3: off
    uint8_t nixie_brightness;
};

struct TubeProtectionSettings {
    bool enabled;
    uint8_t start_hour;
    uint8_t start_minute;
    uint8_t end_hour;
    uint8_t end_minute;
    BacklightProfile profile;
};

struct ClockSettings {
    uint16_t version;
    int8_t tz_offset_hours;
    bool alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_second;
    uint16_t alarm_track;
    uint8_t backlight_r;
    uint8_t backlight_g;
    uint8_t backlight_b;
    uint8_t backlight_brightness;
    uint8_t backlight_effect; // 0: static, 1: breath, 2: rainbow, 3: off
    uint8_t volume;
    bool rtc_calibrated; // true after user/web explicitly sets RTC time
    BacklightProfile profiles[kBacklightProfileCount];
    uint8_t active_profile_index;
    TubeProtectionSettings protection;
};

struct BatteryStatus {
    uint8_t soc;
    uint8_t soh;
    uint16_t battery_voltage_mv;
    int16_t battery_current_ma;
    bool valid;
    TickType_t updated_at;
};

struct TimeStatus {
    time_t unix_utc;
    bool valid;
};

struct SystemSnapshot {
    ClockSettings settings;
    BatteryStatus battery;
    TimeStatus time;
};

class SystemState
{
public:
    SystemState();

    static ClockSettings defaults();

    bool load();
    bool save_settings();

    void set_settings(const ClockSettings &settings);
    bool get_settings(ClockSettings *out) const;

    void update_battery(const BatteryStatus &status);
    bool get_battery(BatteryStatus *out) const;

    void update_time(time_t unix_utc, bool valid);
    bool get_time(TimeStatus *out) const;

    void get_snapshot(SystemSnapshot *out) const;

private:
    mutable portMUX_TYPE mux_;
    ClockSettings settings_;
    BatteryStatus battery_;
    TimeStatus time_;
    uint8_t reserved_[16];
};
