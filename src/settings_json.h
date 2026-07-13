#pragma once

#include "cJSON.h"
#include "system_state.h"
#include <ctime>
#include <string>

struct SettingsJsonError {
    std::string code;
    std::string field;
    std::string message;
};

struct ParsedSettingsUpdate {
    ClockSettings settings;
    struct tm local_time;
    bool has_local_time;
};

cJSON *settings_to_json(const ClockSettings &settings);
bool parse_settings_update(const cJSON *root, const ClockSettings &current,
                           ParsedSettingsUpdate *update, SettingsJsonError *error);
