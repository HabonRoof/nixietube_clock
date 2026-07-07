#include "system_state.h"
#include "nvs.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {
constexpr const char *kNamespace = "clock_cfg";
constexpr const char *kBlobKey = "settings";
// Size of persisted ClockSettings before profiles were added (v4).
constexpr size_t kSettingsV4Size = offsetof(ClockSettings, profiles);

struct TubeProtectionPeriodV6 {
    bool enabled;
    uint8_t start_hour;
    uint8_t start_minute;
    uint8_t end_hour;
    uint8_t end_minute;
    bool clock_on;
    uint8_t profile_index;
};

struct TubeProtectionSettingsV8 {
    bool enabled;
    uint8_t start_hour;
    uint8_t start_minute;
    uint8_t end_hour;
    uint8_t end_minute;
    BacklightProfile profile;
};

struct ClockSettingsV6 {
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
    uint8_t backlight_effect;
    uint8_t volume;
    bool rtc_calibrated;
    BacklightProfile profiles[kBacklightProfileCount];
    uint8_t active_profile_index;
    TubeProtectionPeriodV6 protection_periods[3];
};

constexpr size_t kSettingsV6Size = sizeof(ClockSettingsV6);
constexpr size_t kSettingsV7ProtectionOffset = offsetof(ClockSettings, protection);

void init_default_protection(ClockSettings *settings)
{
    settings->protection = TubeProtectionSettings{
        .enabled = true,
        .start_hour = 0,
        .start_minute = 0,
        .end_hour = 7,
        .end_minute = 0,
        .nixie_brightness = 128,
    };
}

void migrate_protection_from_v8(ClockSettings *settings, const uint8_t *buffer, size_t stored_size)
{
    if (stored_size < offsetof(ClockSettings, protection) + sizeof(TubeProtectionSettingsV8)) {
        init_default_protection(settings);
        return;
    }

    TubeProtectionSettingsV8 old = {};
    std::memcpy(&old, buffer + offsetof(ClockSettings, protection), sizeof(old));

    settings->protection.enabled = old.enabled;
    settings->protection.start_hour = old.start_hour;
    settings->protection.start_minute = old.start_minute;
    settings->protection.end_hour = old.end_hour;
    settings->protection.end_minute = old.end_minute;
    settings->protection.nixie_brightness = old.profile.nixie_brightness;
}

void migrate_protection_from_v6(ClockSettings *settings, const uint8_t *buffer, size_t stored_size)
{
    if (stored_size < kSettingsV6Size) {
        init_default_protection(settings);
        return;
    }

    TubeProtectionPeriodV6 period0 = {};
    std::memcpy(&period0,
                buffer + offsetof(ClockSettingsV6, protection_periods),
                sizeof(period0));

    init_default_protection(settings);
    settings->protection.enabled = period0.enabled;
    settings->protection.start_hour = period0.start_hour;
    settings->protection.start_minute = period0.start_minute;
    settings->protection.end_hour = period0.end_hour;
    settings->protection.end_minute = period0.end_minute;
}

void init_profiles_from_backlight(ClockSettings *settings)
{
    for (uint8_t i = 0; i < kBacklightProfileCount; ++i) {
        settings->profiles[i] = BacklightProfile{
            .r = settings->backlight_r,
            .g = settings->backlight_g,
            .b = settings->backlight_b,
            .backlight_brightness = settings->backlight_brightness,
            .backlight_effect = settings->backlight_effect,
            .nixie_brightness = 255,
            .nixie_transition = 0,
        };
    }
    settings->active_profile_index = 0;
}
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
    ClockSettings settings = {
        .version = kSettingsVersion,
        .tz_offset_hours = 8,
        .alarm_enabled = false,
        .alarm_hour = 7,
        .alarm_minute = 0,
        .alarm_second = 0,
        .alarm_track = 1,
        .backlight_r = 128,
        .backlight_g = 128,
        .backlight_b = 128,
        .backlight_brightness = 128,
        .backlight_effect = 1,
        .volume = 15,
        .rtc_calibrated = false,
        .profiles = {},
        .active_profile_index = 0,
    };
    init_profiles_from_backlight(&settings);
    init_default_protection(&settings);
    return settings;
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

    uint8_t buffer[128];
    if (stored_size > sizeof(buffer)) {
        stored_size = sizeof(buffer);
    }
    err = nvs_get_blob(handle, kBlobKey, buffer, &stored_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        return false;
    }

    const size_t prefix_len = std::min(stored_size, kSettingsV7ProtectionOffset);
    std::memcpy(&settings, buffer, prefix_len);
    const uint16_t stored_version = settings.version;
    bool needs_save = false;

    if (stored_size < offsetof(ClockSettings, backlight_effect)) {
        settings.backlight_effect = 1;
    }
    if (stored_size < offsetof(ClockSettings, volume)) {
        settings.volume = 15;
    }
    if (stored_size < sizeof(ClockSettings)) {
        settings.rtc_calibrated = false;
    }
    if (stored_size < offsetof(ClockSettings, alarm_track)) {
        settings.alarm_track = 1;
    }
    if (stored_size < kSettingsV4Size) {
        init_profiles_from_backlight(&settings);
    }
    if (stored_version < 9 &&
        stored_size >= offsetof(ClockSettings, protection) + sizeof(TubeProtectionSettingsV8)) {
        migrate_protection_from_v8(&settings, buffer, stored_size);
        needs_save = true;
    } else if (stored_size >= sizeof(ClockSettings)) {
        std::memcpy(&settings.protection,
                    buffer + offsetof(ClockSettings, protection),
                    sizeof(TubeProtectionSettings));
    } else if (stored_size >= kSettingsV6Size) {
        migrate_protection_from_v6(&settings, buffer, stored_size);
    } else {
        init_default_protection(&settings);
    }
    if (stored_size < sizeof(ClockSettings)) {
        for (uint8_t i = 0; i < kBacklightProfileCount; ++i) {
            settings.profiles[i].nixie_transition = 0;
        }
    }
    if (stored_version < 8) {
        settings.volume = 15;
        needs_save = true;
    }
    if (settings.volume > 30) {
        settings.volume = 15;
        needs_save = true;
    }
    settings.version = kSettingsVersion;

    portENTER_CRITICAL(&mux_);
    settings_ = settings;
    portEXIT_CRITICAL(&mux_);

    if (needs_save) {
        save_settings();
    }
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
