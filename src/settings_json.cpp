#include "settings_json.h"

#include "dfplayer_mini.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
constexpr int kMinTimezoneOffset = -12;
constexpr int kMaxTimezoneOffset = 14;
constexpr int kSettingsSchemaVersion = 1;

bool fail(SettingsJsonError *error, const char *code, const std::string &field,
          const char *message)
{
    if (error) {
        error->code = code;
        error->field = field;
        error->message = message;
    }
    return false;
}

bool key_allowed(const char *key, std::initializer_list<const char *> allowed)
{
    if (!key) {
        return false;
    }
    for (const char *candidate : allowed) {
        if (std::strcmp(key, candidate) == 0) {
            return true;
        }
    }
    return false;
}

bool reject_unknown(const cJSON *object, std::initializer_list<const char *> allowed,
                    const std::string &path, SettingsJsonError *error)
{
    for (const cJSON *item = object ? object->child : nullptr; item; item = item->next) {
        if (!key_allowed(item->string, allowed)) {
            const std::string field = path.empty() ? item->string : path + "." + item->string;
            return fail(error, "unknown_field", field, "field is not supported");
        }
        for (const cJSON *other = item->next; other; other = other->next) {
            if (item->string && other->string && std::strcmp(item->string, other->string) == 0) {
                const std::string field = path.empty() ? item->string : path + "." + item->string;
                return fail(error, "duplicate_field", field, "field must not appear more than once");
            }
        }
    }
    return true;
}

const cJSON *field(const cJSON *object, const char *name)
{
    return cJSON_GetObjectItemCaseSensitive(object, name);
}

bool require_object(const cJSON *value, const std::string &path, SettingsJsonError *error)
{
    return cJSON_IsObject(value) || fail(error, "invalid_type", path, "must be an object");
}

bool require_nonempty_object(const cJSON *value, const std::string &path, SettingsJsonError *error)
{
    return require_object(value, path, error) &&
           (value->child || fail(error, "empty_update", path, "object must contain a setting"));
}

bool read_integer(const cJSON *value, const std::string &path, int minimum, int maximum,
                  int *out, SettingsJsonError *error)
{
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        std::floor(value->valuedouble) != value->valuedouble) {
        return fail(error, "invalid_type", path, "must be an integer");
    }
    if (value->valuedouble < minimum || value->valuedouble > maximum) {
        char message[80];
        std::snprintf(message, sizeof(message), "must be an integer from %d to %d", minimum, maximum);
        return fail(error, "out_of_range", path, message);
    }
    *out = static_cast<int>(value->valuedouble);
    return true;
}

bool read_bool(const cJSON *value, const std::string &path, bool *out, SettingsJsonError *error)
{
    if (!cJSON_IsBool(value)) {
        return fail(error, "invalid_type", path, "must be a boolean");
    }
    *out = cJSON_IsTrue(value);
    return true;
}

bool read_string(const cJSON *value, const std::string &path, const char **out,
                 SettingsJsonError *error)
{
    if (!cJSON_IsString(value) || !value->valuestring) {
        return fail(error, "invalid_type", path, "must be a string");
    }
    *out = value->valuestring;
    return true;
}

const char *effect_name(uint8_t effect)
{
    switch (effect) {
        case 0: return "static";
        case 1: return "breath";
        case 2: return "rainbow";
        case 3: return "off";
        default: return "static";
    }
}

bool parse_effect(const cJSON *value, const std::string &path, uint8_t *out,
                  SettingsJsonError *error)
{
    const char *text = nullptr;
    if (!read_string(value, path, &text, error)) {
        return false;
    }
    const char *names[] = {"static", "breath", "rainbow", "off"};
    for (uint8_t i = 0; i < 4; ++i) {
        if (std::strcmp(text, names[i]) == 0) {
            *out = i;
            return true;
        }
    }
    return fail(error, "invalid_value", path, "must be static, breath, rainbow, or off");
}

bool parse_transition(const cJSON *value, const std::string &path, uint8_t *out,
                      SettingsJsonError *error)
{
    const char *text = nullptr;
    if (!read_string(value, path, &text, error)) {
        return false;
    }
    if (std::strcmp(text, "instant") == 0) {
        *out = 0;
        return true;
    }
    if (std::strcmp(text, "fade") == 0) {
        *out = 1;
        return true;
    }
    return fail(error, "invalid_value", path, "must be instant or fade");
}

bool leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool parse_local_time(const char *text, struct tm *out)
{
    int year, month, day, hour, minute, second;
    char extra;
    if (!text || std::sscanf(text, "%d-%d-%d %d:%d:%d%c", &year, &month, &day,
                             &hour, &minute, &second, &extra) != 6) {
        return false;
    }
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    const int month_days[] = {31, leap_year(year) ? 29 : 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
    if (day < 1 || day > month_days[month - 1]) {
        return false;
    }
    *out = {};
    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = day;
    out->tm_hour = hour;
    out->tm_min = minute;
    out->tm_sec = second;
    out->tm_isdst = 0;
    return true;
}

bool parse_hms(const char *text, uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    int h, m, s;
    char extra;
    if (!text || std::sscanf(text, "%d:%d:%d%c", &h, &m, &s, &extra) != 3 ||
        h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) {
        return false;
    }
    *hour = static_cast<uint8_t>(h);
    *minute = static_cast<uint8_t>(m);
    *second = static_cast<uint8_t>(s);
    return true;
}

bool parse_hm(const char *text, uint8_t *hour, uint8_t *minute)
{
    int h, m;
    char extra;
    if (!text || std::sscanf(text, "%d:%d%c", &h, &m, &extra) != 2 ||
        h < 0 || h > 23 || m < 0 || m > 59) {
        return false;
    }
    *hour = static_cast<uint8_t>(h);
    *minute = static_cast<uint8_t>(m);
    return true;
}

cJSON *display_json(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness, uint8_t effect,
                    uint8_t nixie_brightness, uint8_t transition)
{
    cJSON *display = cJSON_CreateObject();
    cJSON *backlight = display ? cJSON_AddObjectToObject(display, "backlight") : nullptr;
    cJSON *color = backlight ? cJSON_AddObjectToObject(backlight, "color") : nullptr;
    cJSON *nixie = display ? cJSON_AddObjectToObject(display, "nixie") : nullptr;
    if (!display || !backlight || !color || !nixie ||
        !cJSON_AddNumberToObject(color, "r", r) || !cJSON_AddNumberToObject(color, "g", g) ||
        !cJSON_AddNumberToObject(color, "b", b) ||
        !cJSON_AddNumberToObject(backlight, "brightness", brightness) ||
        !cJSON_AddStringToObject(backlight, "effect", effect_name(effect)) ||
        !cJSON_AddNumberToObject(nixie, "brightness", nixie_brightness) ||
        !cJSON_AddStringToObject(nixie, "transition", transition ? "fade" : "instant")) {
        cJSON_Delete(display);
        return nullptr;
    }
    return display;
}

bool parse_display(const cJSON *display, ClockSettings *settings, uint8_t *nixie_brightness,
                   uint8_t *transition, bool *nixie_changed, SettingsJsonError *error)
{
    if (!require_nonempty_object(display, "display", error) ||
        !reject_unknown(display, {"backlight", "nixie"}, "display", error)) {
        return false;
    }
    const cJSON *backlight = field(display, "backlight");
    if (backlight) {
        if (!require_nonempty_object(backlight, "display.backlight", error) ||
            !reject_unknown(backlight, {"color", "brightness", "effect"},
                            "display.backlight", error)) {
            return false;
        }
        const cJSON *color = field(backlight, "color");
        if (color) {
            if (!require_nonempty_object(color, "display.backlight.color", error) ||
                !reject_unknown(color, {"r", "g", "b"}, "display.backlight.color", error)) {
                return false;
            }
            struct ColorTarget { const char *name; uint8_t *target; } targets[] = {
                {"r", &settings->backlight_r}, {"g", &settings->backlight_g},
                {"b", &settings->backlight_b}};
            for (const auto &target : targets) {
                const cJSON *value = field(color, target.name);
                if (value) {
                    int parsed;
                    if (!read_integer(value, std::string("display.backlight.color.") + target.name,
                                      0, 255, &parsed, error)) return false;
                    *target.target = static_cast<uint8_t>(parsed);
                }
            }
        }
        const cJSON *brightness = field(backlight, "brightness");
        if (brightness) {
            int parsed;
            if (!read_integer(brightness, "display.backlight.brightness", 0, 255, &parsed, error))
                return false;
            settings->backlight_brightness = static_cast<uint8_t>(parsed);
        }
        const cJSON *effect = field(backlight, "effect");
        if (effect && !parse_effect(effect, "display.backlight.effect", &settings->backlight_effect, error))
            return false;
    }
    const cJSON *nixie = field(display, "nixie");
    if (nixie) {
        if (!require_nonempty_object(nixie, "display.nixie", error) ||
            !reject_unknown(nixie, {"brightness", "transition"}, "display.nixie", error)) {
            return false;
        }
        const cJSON *brightness = field(nixie, "brightness");
        if (brightness) {
            int parsed;
            if (!read_integer(brightness, "display.nixie.brightness", 0, 255, &parsed, error))
                return false;
            *nixie_brightness = static_cast<uint8_t>(parsed);
            *nixie_changed = true;
        }
        const cJSON *mode = field(nixie, "transition");
        if (mode) {
            if (!parse_transition(mode, "display.nixie.transition", transition, error)) return false;
            *nixie_changed = true;
        }
    }
    return true;
}
}

cJSON *settings_to_json(const ClockSettings &settings)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *clock = root ? cJSON_AddObjectToObject(root, "clock") : nullptr;
    cJSON *alarm = root ? cJSON_AddObjectToObject(root, "alarm") : nullptr;
    cJSON *audio = root ? cJSON_AddObjectToObject(root, "audio") : nullptr;
    cJSON *profiles = root ? cJSON_AddObjectToObject(root, "profiles") : nullptr;
    cJSON *items = profiles ? cJSON_AddArrayToObject(profiles, "items") : nullptr;
    cJSON *hibernation = root ? cJSON_AddObjectToObject(root, "hibernation") : nullptr;
    const uint8_t active = settings.active_profile_index % kBacklightProfileCount;
    const BacklightProfile &active_profile = settings.profiles[active];
    cJSON *display = display_json(settings.backlight_r, settings.backlight_g, settings.backlight_b,
                                  settings.backlight_brightness, settings.backlight_effect,
                                  active_profile.nixie_brightness, active_profile.nixie_transition);
    if (!root || !clock || !alarm || !audio || !profiles || !items || !hibernation || !display ||
        !cJSON_AddNumberToObject(root, "schema_version", kSettingsSchemaVersion) ||
        !cJSON_AddNumberToObject(clock, "timezone_offset_hours", settings.tz_offset_hours) ||
        !cJSON_AddBoolToObject(clock, "rtc_calibrated", settings.rtc_calibrated)) {
        cJSON_Delete(display);
        cJSON_Delete(root);
        return nullptr;
    }
    cJSON_AddItemToObject(root, "display", display);

    char alarm_time[16];
    std::snprintf(alarm_time, sizeof(alarm_time), "%02u:%02u:%02u", settings.alarm_hour,
                  settings.alarm_minute, settings.alarm_second);
    char start[8], end[8];
    std::snprintf(start, sizeof(start), "%02u:%02u", settings.hibernation.start_hour,
                  settings.hibernation.start_minute);
    std::snprintf(end, sizeof(end), "%02u:%02u", settings.hibernation.end_hour,
                  settings.hibernation.end_minute);
    if (!cJSON_AddBoolToObject(alarm, "enabled", settings.alarm_enabled) ||
        !cJSON_AddStringToObject(alarm, "time", alarm_time) ||
        !cJSON_AddNumberToObject(alarm, "track", settings.alarm_track) ||
        !cJSON_AddNumberToObject(audio, "volume", settings.volume) ||
        !cJSON_AddNumberToObject(profiles, "active_index", active) ||
        !cJSON_AddBoolToObject(hibernation, "enabled", settings.hibernation.enabled) ||
        !cJSON_AddStringToObject(hibernation, "start", start) ||
        !cJSON_AddStringToObject(hibernation, "end", end)) {
        cJSON_Delete(root);
        return nullptr;
    }
    for (uint8_t i = 0; i < kBacklightProfileCount; ++i) {
        const BacklightProfile &profile = settings.profiles[i];
        cJSON *item = cJSON_CreateObject();
        cJSON *profile_display = display_json(profile.r, profile.g, profile.b,
                                              profile.backlight_brightness, profile.backlight_effect,
                                              profile.nixie_brightness, profile.nixie_transition);
        if (!item || !profile_display || !cJSON_AddNumberToObject(item, "index", i)) {
            cJSON_Delete(item);
            cJSON_Delete(profile_display);
            cJSON_Delete(root);
            return nullptr;
        }
        cJSON_AddItemToObject(item, "display", profile_display);
        cJSON_AddItemToArray(items, item);
    }
    return root;
}

bool parse_settings_update(const cJSON *root, const ClockSettings &current,
                           ParsedSettingsUpdate *update, SettingsJsonError *error)
{
    if (!update || !cJSON_IsObject(root)) {
        return fail(error, "invalid_type", "", "root must be an object");
    }
    if (!reject_unknown(root, {"clock", "alarm", "display", "audio", "profiles", "hibernation"},
                        "", error)) {
        return false;
    }
    if (!root->child) {
        return fail(error, "empty_update", "", "at least one settings field is required");
    }

    update->settings = current;
    update->local_time = {};
    update->has_local_time = false;
    ClockSettings &settings = update->settings;
    const uint8_t active = settings.active_profile_index % kBacklightProfileCount;
    uint8_t nixie_brightness = settings.profiles[active].nixie_brightness;
    uint8_t transition = settings.profiles[active].nixie_transition;
    bool nixie_changed = false;
    int save_index = -1;
    int load_index = -1;

    const cJSON *clock = field(root, "clock");
    if (clock) {
        if (!require_nonempty_object(clock, "clock", error) ||
            !reject_unknown(clock, {"timezone_offset_hours", "local_time"}, "clock", error)) return false;
        const cJSON *timezone = field(clock, "timezone_offset_hours");
        if (timezone) {
            int value;
            if (!read_integer(timezone, "clock.timezone_offset_hours", kMinTimezoneOffset,
                              kMaxTimezoneOffset, &value, error)) return false;
            settings.tz_offset_hours = static_cast<int8_t>(value);
        }
        const cJSON *local_time = field(clock, "local_time");
        if (local_time) {
            const char *text;
            if (!read_string(local_time, "clock.local_time", &text, error)) return false;
            if (!parse_local_time(text, &update->local_time))
                return fail(error, "invalid_value", "clock.local_time",
                            "must be a valid date in YYYY-MM-DD HH:MM:SS format");
            update->has_local_time = true;
            settings.rtc_calibrated = true;
        }
    }

    const cJSON *alarm = field(root, "alarm");
    if (alarm) {
        if (!require_nonempty_object(alarm, "alarm", error) ||
            !reject_unknown(alarm, {"enabled", "time", "track"}, "alarm", error)) return false;
        const cJSON *enabled = field(alarm, "enabled");
        if (enabled && !read_bool(enabled, "alarm.enabled", &settings.alarm_enabled, error)) return false;
        const cJSON *time = field(alarm, "time");
        if (time) {
            const char *text;
            if (!read_string(time, "alarm.time", &text, error)) return false;
            if (!parse_hms(text, &settings.alarm_hour, &settings.alarm_minute, &settings.alarm_second))
                return fail(error, "invalid_value", "alarm.time", "must be a valid HH:MM:SS time");
        }
        const cJSON *track = field(alarm, "track");
        if (track) {
            int value;
            if (!read_integer(track, "alarm.track", kDfPlayerMp3MinFile, kDfPlayerMp3MaxFile,
                              &value, error)) return false;
            settings.alarm_track = static_cast<uint16_t>(value);
        }
    }

    const cJSON *display = field(root, "display");
    if (display && !parse_display(display, &settings, &nixie_brightness, &transition,
                                  &nixie_changed, error)) return false;

    const cJSON *audio = field(root, "audio");
    if (audio) {
        if (!require_nonempty_object(audio, "audio", error) ||
            !reject_unknown(audio, {"volume"}, "audio", error)) return false;
        const cJSON *volume = field(audio, "volume");
        if (volume) {
            int value;
            if (!read_integer(volume, "audio.volume", 0, 30, &value, error)) return false;
            settings.volume = static_cast<uint8_t>(value);
        }
    }

    const cJSON *profiles = field(root, "profiles");
    if (profiles) {
        if (!require_nonempty_object(profiles, "profiles", error) ||
            !reject_unknown(profiles, {"save_index", "active_index"}, "profiles", error)) {
            return false;
        }
        const cJSON *save = field(profiles, "save_index");
        if (save &&
            !read_integer(save, "profiles.save_index", 0, kBacklightProfileCount - 1, &save_index,
                          error)) {
            return false;
        }
        const cJSON *active = field(profiles, "active_index");
        if (active &&
            !read_integer(active, "profiles.active_index", 0, kBacklightProfileCount - 1,
                          &load_index, error)) {
            return false;
        }
    }

    const cJSON *hibernation = field(root, "hibernation");
    if (hibernation) {
        if (!require_nonempty_object(hibernation, "hibernation", error) ||
            !reject_unknown(hibernation, {"enabled", "start", "end"}, "hibernation", error)) return false;
        const cJSON *enabled = field(hibernation, "enabled");
        if (enabled && !read_bool(enabled, "hibernation.enabled", &settings.hibernation.enabled, error))
            return false;
        const cJSON *start = field(hibernation, "start");
        if (start) {
            const char *text;
            if (!read_string(start, "hibernation.start", &text, error)) return false;
            if (!parse_hm(text, &settings.hibernation.start_hour, &settings.hibernation.start_minute))
                return fail(error, "invalid_value", "hibernation.start", "must be a valid HH:MM time");
        }
        const cJSON *end = field(hibernation, "end");
        if (end) {
            const char *text;
            if (!read_string(end, "hibernation.end", &text, error)) return false;
            if (!parse_hm(text, &settings.hibernation.end_hour, &settings.hibernation.end_minute))
                return fail(error, "invalid_value", "hibernation.end", "must be a valid HH:MM time");
        }
    }

    if (save_index >= 0) {
        settings.profiles[save_index] = BacklightProfile{
            .r = settings.backlight_r, .g = settings.backlight_g, .b = settings.backlight_b,
            .backlight_brightness = settings.backlight_brightness,
            .backlight_effect = settings.backlight_effect,
            .nixie_brightness = nixie_brightness, .nixie_transition = transition};
        settings.active_profile_index = static_cast<uint8_t>(save_index);
    }
    if (load_index >= 0) {
        const BacklightProfile &profile = settings.profiles[load_index];
        settings.backlight_r = profile.r;
        settings.backlight_g = profile.g;
        settings.backlight_b = profile.b;
        settings.backlight_brightness = profile.backlight_brightness;
        settings.backlight_effect = profile.backlight_effect;
        nixie_brightness = profile.nixie_brightness;
        transition = profile.nixie_transition;
        nixie_changed = true;
        settings.active_profile_index = static_cast<uint8_t>(load_index);
    } else if (save_index < 0 && nixie_changed) {
        settings.profiles[active].nixie_brightness = nixie_brightness;
        settings.profiles[active].nixie_transition = transition;
    }
    return true;
}
