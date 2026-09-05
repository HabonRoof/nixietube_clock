#pragma once

#include <cstdint>
#include <ctime>

struct WifiStaCredentials {
    char ssid[33];
    char password[65];
    bool configured;
};

struct WifiNtpStatus {
    bool configured;
    time_t last_ntp_unix;
    bool last_ntp_success;
};

bool wifi_credentials_load(WifiStaCredentials *out);
bool wifi_credentials_save(const char *ssid, const char *password);
bool wifi_credentials_clear();

bool wifi_ntp_status_load(WifiNtpStatus *out);
bool wifi_ntp_status_save(time_t unix_utc, bool success);
