#include "wifi_credentials.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <cstring>

namespace {
constexpr const char *kTag = "WifiCred";
constexpr const char *kNamespace = "wifi_sta";
constexpr const char *kKeySsid = "ssid";
constexpr const char *kKeyPass = "pass";
constexpr const char *kKeyConfigured = "cfg";
constexpr const char *kKeyLastNtp = "last_ntp";
constexpr const char *kKeyLastNtpOk = "last_ok";
} // namespace

static bool open_namespace(nvs_handle_t *handle, nvs_open_mode_t mode)
{
    const esp_err_t err = nvs_open(kNamespace, mode, handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool wifi_credentials_load(WifiStaCredentials *out)
{
    if (!out) {
        return false;
    }
    std::memset(out, 0, sizeof(*out));

    nvs_handle_t handle = 0;
    if (!open_namespace(&handle, NVS_READONLY)) {
        return false;
    }

    size_t ssid_len = sizeof(out->ssid);
    size_t pass_len = sizeof(out->password);
    uint8_t configured = 0;

    if (nvs_get_str(handle, kKeySsid, out->ssid, &ssid_len) != ESP_OK) {
        nvs_close(handle);
        return true;
    }
    nvs_get_str(handle, kKeyPass, out->password, &pass_len);
    nvs_get_u8(handle, kKeyConfigured, &configured);
    nvs_close(handle);

    out->configured = configured != 0 && out->ssid[0] != '\0';
    return true;
}

bool wifi_credentials_save(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!open_namespace(&handle, NVS_READWRITE)) {
        return false;
    }

    esp_err_t err = nvs_set_str(handle, kKeySsid, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kKeyPass, password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kKeyConfigured, 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool wifi_credentials_clear()
{
    nvs_handle_t handle = 0;
    if (!open_namespace(&handle, NVS_READWRITE)) {
        return false;
    }

    nvs_erase_key(handle, kKeySsid);
    nvs_erase_key(handle, kKeyPass);
    nvs_set_u8(handle, kKeyConfigured, 0);
    const esp_err_t err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool wifi_ntp_status_load(WifiNtpStatus *out)
{
    if (!out) {
        return false;
    }
    std::memset(out, 0, sizeof(*out));

    WifiStaCredentials creds = {};
    wifi_credentials_load(&creds);
    out->configured = creds.configured;

    nvs_handle_t handle = 0;
    if (!open_namespace(&handle, NVS_READONLY)) {
        return false;
    }

    int64_t last = 0;
    uint8_t ok = 0;
    nvs_get_i64(handle, kKeyLastNtp, &last);
    nvs_get_u8(handle, kKeyLastNtpOk, &ok);
    nvs_close(handle);

    out->last_ntp_unix = static_cast<time_t>(last);
    out->last_ntp_success = ok != 0;
    return true;
}

bool wifi_ntp_status_save(time_t unix_utc, bool success)
{
    nvs_handle_t handle = 0;
    if (!open_namespace(&handle, NVS_READWRITE)) {
        return false;
    }

    esp_err_t err = nvs_set_i64(handle, kKeyLastNtp, static_cast<int64_t>(unix_utc));
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kKeyLastNtpOk, success ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}
