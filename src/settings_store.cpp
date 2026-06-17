#include "settings_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr const char *kNamespace = "clock_cfg";
constexpr const char *kBlobKey = "settings";
}

SettingsStore::SettingsStore() = default;

ClockSettings SettingsStore::defaults()
{
    return ClockSettings{
        .version = kSettingsVersion,
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

    // Query the actual stored size so we can migrate older/newer layouts
    // instead of discarding them.
    size_t stored_size = 0;
    err = nvs_get_blob(handle, kBlobKey, nullptr, &stored_size);
    if (err == ESP_ERR_NVS_NOT_FOUND || stored_size == 0) {
        nvs_close(handle);
        *out_settings = defaults();
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

    // Overlay the persisted prefix onto a defaults base (layout is
    // append-only), then stamp the current version. Any newly added fields
    // keep their default values.
    size_t copy_len = std::min(stored_size, sizeof(ClockSettings));
    std::memcpy(&settings, buffer, copy_len);
    settings.version = kSettingsVersion;

    *out_settings = settings;
    return true;
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
