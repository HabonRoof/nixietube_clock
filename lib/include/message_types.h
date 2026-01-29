#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "color_model.h"
#include "gasgauge_driver.h"
#include "powermonitor_driver.h"

// --- Audio Daemon Messages ---
enum class AudioCmd : uint8_t
{
    PLAY_TRACK,
    STOP,
    PAUSE,
    RESUME,
    SET_VOLUME,
    VOLUME_UP,
    VOLUME_DOWN,
    NEXT,
    PREVIOUS
};

struct AudioMessage
{
    AudioCmd command;
    union
    {
        uint16_t track_number;
        uint8_t volume;
    } param;
};

// --- Display Daemon Messages ---
enum class DisplayCmd : uint8_t
{
    UPDATE_TIME,
    SET_MODE,
    SET_MANUAL_NUMBER,
    SET_BACKLIGHT_COLOR,
    SET_BACKLIGHT_BRIGHTNESS,
    SET_EFFECT,
    ENABLE_EFFECT,
    UPDATE_BATTERY
};

enum class DisplayMode : uint8_t
{
    CLOCK_HHMMSS,
    DATE_YYMMDD,
    SETTING_MODE,
    MANUAL_DISPLAY,
    OFF
};

struct DisplayMessage
{
    DisplayCmd command;
    union
    {
        struct
        {
            uint8_t h, m, s;
        } time;
        DisplayMode mode;
        uint32_t number;
        struct
        {
            uint8_t r, g, b;
        } color;
        HsvColor hsv;
        uint8_t brightness;
        uint8_t effect_id; // 0: None, 1: Breath, 2: Rainbow, etc.
        GasgaugeData battery;
    } data;
};

// --- System Controller Messages (Input from other tasks/ISRs) ---
enum class SystemEvent : uint8_t
{
    BUTTON_PRESSED,
    ALARM_TRIGGERED,
    WIFI_CONNECTED,
    WIFI_DISCONNECTED,
    RTC_UPDATE,
    CLI_COMMAND,
    BATTERY_UPDATE,
    POWER_UPDATE
};

enum class CliCommandType : uint8_t
{
    SET_NIXIE,
    SET_BACKLIGHT
};

struct CliData
{
    CliCommandType type;
    uint32_t value; // For SET_NIXIE
    struct {
        uint8_t r, g, b;
        uint8_t brightness;
        bool has_color;
        bool has_brightness;
    } backlight;
};

struct SystemMessage
{
    SystemEvent event;
    union
    {
        uint8_t button_id;
        CliData cli;
        GasgaugeData battery;
        PowerMonitorData power;
        // TODO: Add other features
        // Add other event data as needed
    } data;
};