#pragma once

#include <cstdint>

struct ClockSettings {
    int8_t tz_offset_hours;
    bool alarm_enabled;
    uint8_t alarm_hour;
    uint8_t alarm_minute;
    uint8_t alarm_second;
    uint8_t backlight_r;
    uint8_t backlight_g;
    uint8_t backlight_b;
    uint8_t backlight_brightness;
    uint8_t volume;
};

class SettingsStore
{
public:
    SettingsStore();

    bool load(ClockSettings *out_settings);
    bool save(const ClockSettings &settings);

    static ClockSettings defaults();
};
