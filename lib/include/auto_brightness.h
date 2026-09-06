#pragma once

#include <cstdint>

constexpr uint16_t kAmbientFullScale = 1024;
constexpr uint16_t kAmbientMinFactor = 51;

constexpr uint16_t kAutoBrightnessCalVersion = 2;

enum class AutoBrightnessCalMode : uint8_t
{
    Auto = 0,
    Manual = 1,
};

struct AutoBrightnessCalibration
{
    uint16_t version;
    float lux_min;
    float lux_max;
    float k_led;
    float k_nixie;
    AutoBrightnessCalMode mode;
    uint8_t als_gain;
    uint8_t reserved[2];
};

inline AutoBrightnessCalibration auto_brightness_calibration_defaults()
{
    return AutoBrightnessCalibration{
        .version = kAutoBrightnessCalVersion,
        .lux_min = 0.5f,
        .lux_max = 300.0f,
        .k_led = 0.0f,
        .k_nixie = 0.0f,
        .mode = AutoBrightnessCalMode::Auto,
        .als_gain = 0,
        .reserved = {},
    };
}
