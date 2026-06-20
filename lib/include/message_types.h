#pragma once

#include <cstdint>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "color_model.h"
#include "gasgauge_driver.h"
#include "charger_driver.h"
#include "system_state.h"

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
    PREVIOUS,
    QUERY_TRACK_COUNT,
    TOGGLE_TRACK,
    GET_STATUS,
    BOOT_CHIME_PLAY,
    BOOT_CHIME_STOP,
};

enum class AudioPlaybackUiState : uint8_t
{
    STOPPED = 0,
    PLAYING = 1,
    PAUSED = 2,
};

struct AudioDaemonStatus
{
    uint16_t track_count;
    uint16_t current_track;
    AudioPlaybackUiState state;
    bool track_count_valid;
};

// Heap-owned RPC context; survives until daemon completes or caller times out.
struct AudioRpcContext
{
    SemaphoreHandle_t sem;
    bool ok;
    volatile bool abandoned;
};

struct AudioMessage
{
    AudioCmd command;
    union
    {
        uint16_t track_number;
        uint8_t volume;
    } param;
    AudioRpcContext *rpc_ctx;
    AudioDaemonStatus *response_status;
    uint16_t *response_count;
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
    ENABLE_EFFECT
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
        uint8_t effect_id; // 0: static, 1: breath, 2: rainbow, 3: off
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
    SETTINGS_UPDATE,
    CLI_COMMAND,
    BATTERY_UPDATE,
    CHARGER_UPDATE
};

// Carries a settings change (and optionally a new wall-clock time) from the
// web/CLI tasks to the SystemController task, which is the sole owner of the
// RTC and the active settings.
struct SettingsUpdate
{
    ClockSettings settings;
    struct tm local_time; // user-entered local wall-clock time
    bool has_time;
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
        ChargerData charger;
        SettingsUpdate apply;
        // TODO: Add other features
        // Add other event data as needed
    } data;
};