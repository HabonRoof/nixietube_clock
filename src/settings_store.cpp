#include "settings_store.h"
#include "nvs_flash.h"
#include "nvs.h"

namespace {
constexpr const char *kNamespace = "clock_cfg";
constexpr const char *kBlobKey = "settings";
}

SettingsStore::SettingsStore() = default;

ClockSettings SettingsStore::defaults()
{
    return ClockSettings{
        .tz_offset_hours = 8,
        .alarm_enabled = false,
        .alarm_hour = 7,
        .alarm_minute = 0,
        .alarm_second = 0,
        .backlight_r = 0,
        .backlight_g = 255,
        .backlight_b = 255,
        .backlight_brightness = 128,
        .volume = 20,
    };
}

bool SettingsStore::load(ClockSettings *out_settings)
{
    if (!out_settings) {
        return false;
    }

    ClockSettings settings = defaults();

    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_settings = settings;
        return true;
    }
    if (err != ESP_OK) {
        return false;
    }

    size_t required_size = sizeof(ClockSettings);
    err = nvs_get_blob(handle, kBlobKey, &settings, &required_size);
    nvs_close(handle);

    if (err == ESP_OK && required_size == sizeof(ClockSettings)) {
        *out_settings = settings;
        return true;
    }

    *out_settings = defaults();
    return false;
}

bool SettingsStore::save(const ClockSettings &settings)
{
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
