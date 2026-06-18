#include "system_state.h"
#include "nvs.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {
constexpr const char *kNamespace = "clock_cfg";
constexpr const char *kBlobKey = "settings";
// Size of persisted ClockSettings before backlight_effect was added (v1).
constexpr size_t kSettingsV1Size = offsetof(ClockSettings, backlight_effect);
}

SystemState::SystemState()
    : mux_(portMUX_INITIALIZER_UNLOCKED),
      settings_(defaults()),
      battery_{},
      time_{},
      reserved_{}
{
    battery_.valid = false;
    battery_.updated_at = 0;
    time_.unix_utc = 0;
    time_.valid = false;
    std::memset(reserved_, 0, sizeof(reserved_));
}

ClockSettings SystemState::defaults()
{
    return ClockSettings{
        .version = kSettingsVersion,
        .tz_offset_hours = 8,
        .alarm_enabled = false,
        .alarm_hour = 7,
        .alarm_minute = 0,
        .alarm_second = 0,
        .backlight_r = 128,
        .backlight_g = 128,
        .backlight_b = 128,
        .backlight_brightness = 128,
        .backlight_effect = 1,
        .volume = 20,
    };
}

bool SystemState::load()
{
    ClockSettings settings = defaults();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        portENTER_CRITICAL(&mux_);
        settings_ = settings;
        portEXIT_CRITICAL(&mux_);
        return true;
    }
    if (err != ESP_OK) {
        return false;
    }

    size_t stored_size = 0;
    err = nvs_get_blob(handle, kBlobKey, nullptr, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND || stored_size == 0) {
        nvs_close(handle);
        portENTER_CRITICAL(&mux_);
        settings_ = settings;
        portEXIT_CRITICAL(&mux_);
        return true;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    uint8_t buffer[64];
    if (stored_size > sizeof(buffer)) {
        stored_size = sizeof(buffer);
    }
    err = nvs_get_blob(handle, kBlobKey, buffer, &stored_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t copy_len = std::min(stored_size, sizeof(ClockSettings));
    std::memcpy(&settings, buffer, copy_len);
    if (stored_size < sizeof(ClockSettings) && stored_size >= kSettingsV1Size) {
        settings.volume = buffer[kSettingsV1Size - 1];
        settings.backlight_effect = 1;
    }
    settings.version = kSettingsVersion;

    portENTER_CRITICAL(&mux_);
    settings_ = settings;
    portEXIT_CRITICAL(&mux_);
    return true;
}

bool SystemState::save_settings()
{
    ClockSettings settings;
    if (!get_settings(&settings)) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }

    err = nvs_set_blob(handle, kBlobKey, &settings, sizeof(ClockSettings));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    return err == ESP_OK;
}

void SystemState::set_settings(const ClockSettings &settings)
{
    portENTER_CRITICAL(&mux_);
    settings_ = settings;
    portEXIT_CRITICAL(&mux_);
}

bool SystemState::get_settings(ClockSettings *out) const
{
    if (!out) {
        return false;
    }

    portENTER_CRITICAL(&mux_);
    *out = settings_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void SystemState::update_battery(const BatteryStatus &status)
{
    portENTER_CRITICAL(&mux_);
    battery_ = status;
    portEXIT_CRITICAL(&mux_);
}

bool SystemState::get_battery(BatteryStatus *out) const
{
    if (!out) {
        return false;
    }

    portENTER_CRITICAL(&mux_);
    *out = battery_;
    portEXIT_CRITICAL(&mux_);
    return out->valid;
}

void SystemState::update_time(time_t unix_utc, bool valid)
{
    portENTER_CRITICAL(&mux_);
    time_.unix_utc = unix_utc;
    time_.valid = valid;
    portEXIT_CRITICAL(&mux_);
}

bool SystemState::get_time(TimeStatus *out) const
{
    if (!out) {
        return false;
    }

    portENTER_CRITICAL(&mux_);
    *out = time_;
    portEXIT_CRITICAL(&mux_);
    return true;
}

void SystemState::get_snapshot(SystemSnapshot *out) const
{
    if (!out) {
        return;
    }

    portENTER_CRITICAL(&mux_);
    out->settings = settings_;
    out->battery = battery_;
    out->time = time_;
    portEXIT_CRITICAL(&mux_);
}
