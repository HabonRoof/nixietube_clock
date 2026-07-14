#include "daemons/cli_daemon.h"
#include "daemons/audio_daemon.h"
#include "dfplayer_mini.h"
#include "message_types.h"
#include "gasgauge_service.h"
#include "bq27441/bq27441_regs.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "linenoise/linenoise.h"
#include "esp_mac.h"
#include "display_board_config.h"
#include <cstring>
#include <cstdio>
#include <ctime>

static const char *TAG = "CliDaemon";
static SystemController *g_system_controller = nullptr;
static ChargerController *g_charger_controller = nullptr;
static PowerController *g_power_controller = nullptr;
static GasgaugeService *g_gasgauge_service = nullptr;
static SystemState *g_system_state = nullptr;
static AudioDaemon *g_audio_daemon = nullptr;

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#define DEV_BOARD_VERSION "v0.9.1" // Hardcoded for now

// --- Command: set_nixie ---
struct set_nixie_args {
    struct arg_int *number;
    struct arg_end *end;
};

static struct set_nixie_args nixie_args;

static int set_nixie_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&nixie_args);
    if (nerrors > 0) {
        arg_print_errors(stdout, nixie_args.end, "set_nixie");
        return 1;
    }

    if (nixie_args.number->count > 0) {
        uint32_t number = nixie_args.number->ival[0];
        ESP_LOGI(TAG, "Setting Nixie Number: %lu", number);
        
        SystemMessage msg;
        msg.event = SystemEvent::CLI_COMMAND;
        msg.data.cli.type = CliCommandType::SET_NIXIE;
        msg.data.cli.value = number;
        
        if (g_system_controller) {
            xQueueSend(g_system_controller->get_queue(), &msg, 0);
        }
    }
    return 0;
}

// --- Command: set_backlight ---
struct set_backlight_args {
    struct arg_str *rgb;
    struct arg_int *brightness;
    struct arg_end *end;
};

static struct set_backlight_args backlight_args;

static int set_backlight_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&backlight_args);
    if (nerrors > 0) {
        arg_print_errors(stdout, backlight_args.end, "set_backlight");
        return 1;
    }

    SystemMessage msg;
    msg.event = SystemEvent::CLI_COMMAND;
    msg.data.cli.type = CliCommandType::SET_BACKLIGHT;
    msg.data.cli.backlight.has_color = false;
    msg.data.cli.backlight.has_brightness = false;

    if (backlight_args.rgb->count > 0) {
        int r, g, b;
        if (sscanf(backlight_args.rgb->sval[0], "%d,%d,%d", &r, &g, &b) == 3) {
            // TODO: check RGB value is in 255 range
            msg.data.cli.backlight.has_color = true;
            msg.data.cli.backlight.r = r;
            msg.data.cli.backlight.g = g;
            msg.data.cli.backlight.b = b;
            printf("set backlight Color \e[0;31mR:%d, \e[0;32mG:%d, \e[0;34mB:%d\n\e[39m",r, g, b);
        } else {
            printf("Invalid RGB format. Use r,g,b\n");
            printf("For example: 'set_backlight --rgb 255,255,255'");
            return 1;
        }
    }

    if (backlight_args.brightness->count > 0) {
        msg.data.cli.backlight.has_brightness = true;
        // TODO: check brightness value is in 255 range
        msg.data.cli.backlight.brightness = backlight_args.brightness->ival[0];
        printf("set backlight Brightness:%d\n",msg.data.cli.backlight.brightness);
    }

    if (g_system_controller) {
        xQueueSend(g_system_controller->get_queue(), &msg, 0);
    }
    return 0;
}

// --- Command: get_uuid ---
static int get_uuid_func(int argc, char **argv)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("UUID: %02X%02X%02X%02X%02X%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

// --- Command: ggtool ---
struct ggtool_args {
    struct arg_str *subcmd;
    struct arg_str *arg1;
    struct arg_str *arg2;
    struct arg_end *end;
};

static struct ggtool_args ggtool_args;

static int ggtool_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ggtool_args);
    if (nerrors > 0) {
        arg_print_errors(stdout, ggtool_args.end, "ggtool");
        return 1;
    }

    if (!g_gasgauge_service) {
        printf("gasgauge service not ready\n");
        return 1;
    }

    if (ggtool_args.subcmd->count == 0) {
        printf("Usage: ggtool <status|peek|block|config|read|cache> [args...]\n");
        return 1;
    }

    const char *subcmd = ggtool_args.subcmd->sval[0];

    if (strcmp(subcmd, "peek") == 0) {
        if (ggtool_args.arg1->count == 0) {
            printf("Usage: ggtool peek <reg_hex> [len]\n");
            return 1;
        }

        const uint8_t reg = static_cast<uint8_t>(strtoul(ggtool_args.arg1->sval[0], nullptr, 16));
        const size_t len = (ggtool_args.arg2->count > 0)
            ? static_cast<size_t>(strtoul(ggtool_args.arg2->sval[0], nullptr, 10))
            : 1U;
        if (len == 0 || len > 32) {
            printf("len must be 1..32\n");
            return 1;
        }

        uint8_t buf[32] = {};
        if (!g_gasgauge_service->peek_registers(reg, buf, len)) {
            printf("Failed to peek register 0x%02X\n", reg);
            return 1;
        }

        printf("Reg 0x%02X:", reg);
        for (size_t i = 0; i < len; ++i) {
            printf(" %02X", buf[i]);
        }
        printf("\n");

        if (len == 2) {
            const uint16_t word = static_cast<uint16_t>((buf[1] << 8) | buf[0]);
            printf("Word (LE): %u (0x%04X)\n", word, word);
        }
        return 0;
    }

    if (strcmp(subcmd, "block") == 0) {
        const uint8_t class_id = (ggtool_args.arg1->count > 0)
            ? static_cast<uint8_t>(strtoul(ggtool_args.arg1->sval[0], nullptr, 0))
            : static_cast<uint8_t>(82);
        const uint8_t block_index = (ggtool_args.arg2->count > 0)
            ? static_cast<uint8_t>(strtoul(ggtool_args.arg2->sval[0], nullptr, 0))
            : static_cast<uint8_t>(0);

        uint8_t data[32] = {};
        uint8_t checksum = 0;
        if (!g_gasgauge_service->dump_state_block(class_id, block_index, data, &checksum)) {
            printf("Failed to dump state block (class=%u block=%u)\n", class_id, block_index);
            return 1;
        }

        printf("State block class=%u block=%u checksum=0x%02X\n", class_id, block_index, checksum);
        for (uint8_t i = 0; i < 32; ++i) {
            if (i % 16 == 0) {
                printf("%02X:", i);
            }
            printf(" %02X", data[i]);
            if (i % 16 == 15) {
                printf("\n");
            }
        }
        if (32 % 16 != 0) {
            printf("\n");
        }

        const uint16_t design_cap =
            static_cast<uint16_t>((data[9] << 8) | data[10]);
        const uint16_t design_energy =
            static_cast<uint16_t>((data[11] << 8) | data[12]);
        printf("DesignCapacity (offset 9): %u mAh\n", design_cap);
        printf("DesignEnergy (offset 11): %u mWh\n", design_energy);
        return 0;
    }

    if (strcmp(subcmd, "status") == 0) {
        GasgaugeDeviceInfo info;
        if (!g_gasgauge_service->probe_device_info(info)) {
            printf("Failed to probe BQ27441\n");
            return 1;
        }
        printf("Device Type: 0x%04X\n", info.device_type);
        printf("FW Version: 0x%04X\n", info.fw_version);
        printf("Design Capacity: %u mAh\n", info.design_capacity);
        printf("Control Status: 0x%04X\n", info.control_status);
        printf("Flags: 0x%04X\n", info.flags);
        printf("Sealed: %s\n", info.sealed ? "yes" : "no");
        printf("CFGUPMODE: %s\n", (info.flags & bq27441::kFlagCfgUpdate) ? "yes" : "no");
        printf("Battery detected (BAT_DET): %s\n", info.battery_detected ? "yes" : "no");
        printf("Init complete (INITCOMP): %s\n", info.init_complete ? "yes" : "no");
        printf("ITPOR (needs reconfig): %s\n", info.needs_reconfig ? "yes" : "no");
        const bool gauging_ready =
            info.battery_detected &&
            info.init_complete &&
            (info.flags & bq27441::kFlagCfgUpdate) == 0;
        printf("Gauging ready: %s\n", gauging_ready ? "yes" : "no");
        return 0;
    }

    if (strcmp(subcmd, "config") == 0) {
        uint16_t mah = GasgaugeService::kDefaultCapacityMah;
        if (ggtool_args.arg1->count > 0) {
            mah = static_cast<uint16_t>(strtoul(ggtool_args.arg1->sval[0], nullptr, 10));
        }

        const bool ok = g_gasgauge_service->configure_capacity(mah, true);
        if (!ok) {
            printf("Failed to configure design capacity to %u mAh\n", mah);
            return 1;
        }
        printf("Design capacity configured to %u mAh\n", mah);
        return 0;
    }

    if (strcmp(subcmd, "read") == 0) {
        GasgaugeData data;
        if (!g_gasgauge_service->read_data(data)) {
            printf("Failed to read live gasgauge data\n");
            return 1;
        }
        printf("SOC: %u%%\n", data.soc);
        printf("SOH: %u%%\n", data.soh);
        printf("Voltage: %u mV\n", data.voltage_mv);
        printf("Current: %d mA\n", data.current_ma);
        return 0;
    }

    if (strcmp(subcmd, "cache") == 0) {
        if (!g_system_state) {
            printf("system state not ready\n");
            return 1;
        }

        BatteryStatus battery;
        if (!g_system_state->get_battery(&battery)) {
            printf("No cached battery data available\n");
            return 1;
        }

        const TickType_t now = xTaskGetTickCount();
        const TickType_t age_ticks = now - battery.updated_at;
        const uint32_t age_ms = static_cast<uint32_t>(age_ticks * portTICK_PERIOD_MS);

        printf("SOC: %u%%\n", battery.soc);
        printf("SOH: %u%%\n", battery.soh);
        printf("Voltage: %u mV\n", battery.battery_voltage_mv);
        printf("Current: %d mA\n", battery.battery_current_ma);
        printf("Age: %lu ms\n", static_cast<unsigned long>(age_ms));
        return 0;
    }

    printf("Unknown subcommand: %s\n", subcmd);
    printf("Usage: ggtool <status|peek|block|config|read|cache> [args...]\n");
    return 1;
}

// --- DFPlayer helpers ---
static const char *df_audio_state_string(AudioPlaybackUiState state)
{
    switch (state) {
        case AudioPlaybackUiState::PLAYING:
            return "playing";
        case AudioPlaybackUiState::PAUSED:
            return "paused";
        default:
            return "stopped";
    }
}

static bool df_send_audio_cmd(AudioCmd cmd, uint16_t param = 0)
{
    if (!g_audio_daemon) {
        printf("audio daemon not ready\n");
        return false;
    }

    AudioMessage msg = {};
    msg.command = cmd;
    if (cmd == AudioCmd::SET_VOLUME) {
        msg.param.volume = static_cast<uint8_t>(param);
    } else {
        msg.param.track_number = param;
    }

    if (xQueueSend(g_audio_daemon->get_queue(), &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("audio command queue full\n");
        return false;
    }
    return true;
}

static void df_print_track_name(uint16_t track)
{
    char name[20];
    snprintf(name, sizeof(name), "mp3/%04u.mp3", track);
    printf("  %u: %s\n", track, name);
}

// --- Command: dftool ---
struct dftool_args {
    struct arg_str *subcmd;
    struct arg_str *arg1;
    struct arg_str *arg2;
    struct arg_end *end;
};

static struct dftool_args dftool_args;

static int dftool_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&dftool_args);
    if (nerrors > 0) {
        arg_print_errors(stdout, dftool_args.end, "dftool");
        return 1;
    }

    if (!g_audio_daemon) {
        printf("audio daemon not ready\n");
        return 1;
    }

    if (dftool_args.subcmd->count == 0) {
        printf("Usage: dftool <list|status|play|playmp3folder|toggle|pause|resume|stop|next|prev|volume|vol_up|vol_down> [args...]\n");
        return 1;
    }

    const char *subcmd = dftool_args.subcmd->sval[0];

    if (strcmp(subcmd, "list") == 0) {
        uint16_t count = 0;
        if (!g_audio_daemon->rpc_query_tracks(&count)) {
            printf("Failed to query mp3/ folder track count\n");
            return 1;
        }
        printf("mp3/ tracks: %u\n", count);
        for (uint16_t i = 1; i <= count; ++i) {
            df_print_track_name(i);
        }
        return 0;
    }

    if (strcmp(subcmd, "status") == 0) {
        AudioDaemonStatus status = {};
        if (!g_audio_daemon->rpc_query_playback_status(&status)) {
            printf("Failed to query DFPlayer playback status\n");
            return 1;
        }
        printf("Track: %u\n", status.current_track);
        printf("State: %s\n", df_audio_state_string(status.state));
        if (status.track_count_valid) {
            printf("Track count: %u (cached)\n", status.track_count);
        } else {
            printf("Track count: unknown (run dftool list)\n");
        }
        return 0;
    }

    if (strcmp(subcmd, "play") == 0) {
        if (dftool_args.arg1->count == 0) {
            printf("Usage: dftool play <track> [--loop]\n");
            return 1;
        }

        int track = atoi(dftool_args.arg1->sval[0]);
        if (track < kDfPlayerMp3MinFile || track > kDfPlayerMp3MaxFile) {
            printf("Invalid track number (%u-%u)\n", kDfPlayerMp3MinFile, kDfPlayerMp3MaxFile);
            return 1;
        }

        bool loop = dftool_args.arg2->count > 0 &&
                    strcmp(dftool_args.arg2->sval[0], "--loop") == 0;

        // TODO: Looping is unsupported for this command.
        AudioCmd cmd = loop ? AudioCmd::PLAY_TRACK_LOOP : AudioCmd::PLAY_TRACK;
        if (!df_send_audio_cmd(cmd, static_cast<uint16_t>(track))) {
            return 1;
        }
        printf("Playing track %d%s\n", track, loop ? " (loop)" : "");
        return 0;
    }

    if (strcmp(subcmd, "toggle") == 0) {
        if (dftool_args.arg1->count == 0) {
            printf("Usage: dftool toggle <track>\n");
            return 1;
        }

        int track = atoi(dftool_args.arg1->sval[0]);
        if (track < kDfPlayerMp3MinFile || track > kDfPlayerMp3MaxFile) {
            printf("Invalid track number (%u-%u)\n", kDfPlayerMp3MinFile, kDfPlayerMp3MaxFile);
            return 1;
        }

        AudioDaemonStatus status = {};
        if (!g_audio_daemon->rpc_toggle_track(static_cast<uint16_t>(track), &status)) {
            printf("Toggle failed\n");
            return 1;
        }
        printf("Track %u: %s\n", status.current_track, df_audio_state_string(status.state));
        return 0;
    }

    if (strcmp(subcmd, "pause") == 0) {
        if (!df_send_audio_cmd(AudioCmd::PAUSE)) {
            return 1;
        }
        printf("Playback paused\n");
        return 0;
    }

    if (strcmp(subcmd, "resume") == 0) {
        if (!df_send_audio_cmd(AudioCmd::RESUME)) {
            return 1;
        }
        printf("Playback resumed\n");
        return 0;
    }

    if (strcmp(subcmd, "stop") == 0) {
        if (!df_send_audio_cmd(AudioCmd::STOP)) {
            return 1;
        }
        printf("Playback stopped\n");
        return 0;
    }

    if (strcmp(subcmd, "next") == 0) {
        if (!df_send_audio_cmd(AudioCmd::NEXT)) {
            return 1;
        }
        printf("Playing next track\n");
        return 0;
    }

    if (strcmp(subcmd, "prev") == 0 || strcmp(subcmd, "previous") == 0) {
        if (!df_send_audio_cmd(AudioCmd::PREVIOUS)) {
            return 1;
        }
        printf("Playing previous track\n");
        return 0;
    }

    if (strcmp(subcmd, "volume") == 0) {
        if (dftool_args.arg1->count == 0) {
            printf("Usage: dftool volume <0-30>\n");
            return 1;
        }

        int volume = atoi(dftool_args.arg1->sval[0]);
        if (volume < 0 || volume > 30) {
            printf("Invalid volume (0-30)\n");
            return 1;
        }

        if (!df_send_audio_cmd(AudioCmd::SET_VOLUME, static_cast<uint16_t>(volume))) {
            return 1;
        }
        printf("Volume set to %d\n", volume);
        return 0;
    }

    if (strcmp(subcmd, "vol_up") == 0 || strcmp(subcmd, "volume_up") == 0) {
        if (!df_send_audio_cmd(AudioCmd::VOLUME_UP)) {
            return 1;
        }
        printf("Volume increased\n");
        return 0;
    }

    if (strcmp(subcmd, "vol_down") == 0 || strcmp(subcmd, "volume_down") == 0) {
        if (!df_send_audio_cmd(AudioCmd::VOLUME_DOWN)) {
            return 1;
        }
        printf("Volume decreased\n");
        return 0;
    }

    printf("Unknown subcommand: %s\n", subcmd);
    printf("Usage: dftool <list|status|play|toggle|pause|resume|stop|next|prev|volume|vol_up|vol_down> [args...]\n");
    return 1;
}

// --- RTC parse helpers (same formats as web UI) ---
static bool rtc_parse_time(const char *text, struct tm *out_tm)
{
    if (!text || !out_tm) {
        return false;
    }

    int year, month, day, hour, minute, second;
    if (sscanf(text, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_year = year - 1900;
    out_tm->tm_mon = month - 1;
    out_tm->tm_mday = day;
    out_tm->tm_hour = hour;
    out_tm->tm_min = minute;
    out_tm->tm_sec = second;
    out_tm->tm_isdst = 0;
    return true;
}

static bool rtc_parse_alarm(const char *text, uint8_t *h, uint8_t *m, uint8_t *s)
{
    if (!text || !h || !m || !s) {
        return false;
    }
    int hour, minute, second;
    if (sscanf(text, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    *h = static_cast<uint8_t>(hour);
    *m = static_cast<uint8_t>(minute);
    *s = static_cast<uint8_t>(second);
    return true;
}

static ClockSettings rtc_load_settings()
{
    ClockSettings settings = SystemState::defaults();
    if (g_system_state) {
        g_system_state->get_settings(&settings);
    }
    return settings;
}

// --- Command: rtctool ---
struct rtctool_args {
    struct arg_str *subcmd;
    struct arg_str *arg1;
    struct arg_str *arg2;
    struct arg_str *arg3;
    struct arg_str *arg4;
    struct arg_end *end;
};

static struct rtctool_args rtctool_args;

static int rtctool_func(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&rtctool_args);
    if (nerrors > 0) {
        arg_print_errors(stdout, rtctool_args.end, "rtctool");
        return 1;
    }

    if (!g_system_controller) {
        printf("system controller not ready\n");
        return 1;
    }

    if (rtctool_args.subcmd->count == 0) {
        printf("Usage: rtctool <read|set_time|set_alarm|set_tz> [args...]\n");
        return 1;
    }

    const char *subcmd = rtctool_args.subcmd->sval[0];

    if (strcmp(subcmd, "read") == 0) {
        struct tm local_tm = {};
        bool time_valid = false;
        bool osf = false;
        float temperature = 0.0f;
        time_t unix_utc = 0;
        g_system_controller->get_time_status(&local_tm, &time_valid, &osf, &temperature, &unix_utc);

        ClockSettings settings = rtc_load_settings();

        printf("Local time: %04d-%02d-%02d %02d:%02d:%02d\n",
               local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
               local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
        printf("UTC epoch: %lld\n", static_cast<long long>(unix_utc));
        printf("Timezone offset: %+d h\n", settings.tz_offset_hours);
        printf("Time valid: %s\n", time_valid ? "yes" : "no");
        printf("RTC calibrated: %s\n", settings.rtc_calibrated ? "yes" : "no");
        printf("OSF (backup lost): %s\n", osf ? "yes" : "no");
        printf("Temperature: %.2f C\n", temperature);
        printf("Alarm enabled: %s\n", settings.alarm_enabled ? "yes" : "no");
        printf("Alarm time: %02u:%02u:%02u\n",
               settings.alarm_hour, settings.alarm_minute, settings.alarm_second);
        printf("Alarm track: %u\n", settings.alarm_track);
        return 0;
    }

    if (strcmp(subcmd, "set_time") == 0) {
        char datetime[48] = {};
        if (rtctool_args.arg1->count == 0) {
            printf("Usage: rtctool set_time <YYYY-MM-DD HH:MM:SS>\n");
            printf("Example: rtctool set_time 2026-07-06 16:30:00\n");
            return 1;
        }
        if (rtctool_args.arg2->count > 0) {
            snprintf(datetime, sizeof(datetime), "%s %s",
                     rtctool_args.arg1->sval[0], rtctool_args.arg2->sval[0]);
        } else {
            snprintf(datetime, sizeof(datetime), "%s", rtctool_args.arg1->sval[0]);
        }

        struct tm timeinfo = {};
        if (!rtc_parse_time(datetime, &timeinfo)) {
            printf("Invalid datetime. Use YYYY-MM-DD HH:MM:SS (local wall clock)\n");
            return 1;
        }

        ClockSettings settings = rtc_load_settings();
        settings.rtc_calibrated = true;
        g_system_controller->request_settings_update(settings, &timeinfo);
        printf("RTC calibration requested: %s (tz %+d)\n", datetime, settings.tz_offset_hours);
        return 0;
    }

    if (strcmp(subcmd, "set_alarm") == 0) {
        if (rtctool_args.arg1->count == 0) {
            printf("Usage: rtctool set_alarm <HH:MM:SS> [--enable|--disable] [--track <n>]\n");
            return 1;
        }

        uint8_t hour = 0;
        uint8_t minute = 0;
        uint8_t second = 0;
        if (!rtc_parse_alarm(rtctool_args.arg1->sval[0], &hour, &minute, &second)) {
            printf("Invalid alarm time. Use HH:MM:SS\n");
            return 1;
        }

        ClockSettings settings = rtc_load_settings();
        settings.alarm_hour = hour;
        settings.alarm_minute = minute;
        settings.alarm_second = second;

        const char *opts[3] = {};
        int opt_count = 0;
        if (rtctool_args.arg2->count > 0) {
            opts[opt_count++] = rtctool_args.arg2->sval[0];
        }
        if (rtctool_args.arg3->count > 0) {
            opts[opt_count++] = rtctool_args.arg3->sval[0];
        }
        if (rtctool_args.arg4->count > 0) {
            opts[opt_count++] = rtctool_args.arg4->sval[0];
        }

        bool enable_set = false;
        bool disable_set = false;
        for (int i = 0; i < opt_count; ++i) {
            if (strcmp(opts[i], "--enable") == 0) {
                enable_set = true;
            } else if (strcmp(opts[i], "--disable") == 0) {
                disable_set = true;
            } else if (strcmp(opts[i], "--track") == 0) {
                if (i + 1 >= opt_count) {
                    printf("--track requires a value (%u..%u)\n", kDfPlayerMp3MinFile,
                           kDfPlayerMp3MaxFile);
                    return 1;
                }
                const int track = atoi(opts[++i]);
                if (track < kDfPlayerMp3MinFile || track > kDfPlayerMp3MaxFile) {
                    printf("Track must be %u..%u\n", kDfPlayerMp3MinFile, kDfPlayerMp3MaxFile);
                    return 1;
                }
                settings.alarm_track = static_cast<uint16_t>(track);
            } else {
                printf("Unknown option: %s\n", opts[i]);
                return 1;
            }
        }

        if (disable_set && enable_set) {
            printf("Use only one of --enable or --disable\n");
            return 1;
        }
        if (disable_set) {
            settings.alarm_enabled = false;
        } else if (enable_set) {
            settings.alarm_enabled = true;
        } else {
            settings.alarm_enabled = true;
        }

        g_system_controller->request_settings_update(settings, nullptr);
        printf("Alarm update requested: %02u:%02u:%02u %s (track %u)\n",
               settings.alarm_hour, settings.alarm_minute, settings.alarm_second,
               settings.alarm_enabled ? "enabled" : "disabled", settings.alarm_track);
        return 0;
    }

    if (strcmp(subcmd, "set_tz") == 0) {
        if (rtctool_args.arg1->count == 0) {
            printf("Usage: rtctool set_tz <offset_hours>\n");
            printf("Example: rtctool set_tz 8\n");
            return 1;
        }

        const int tz = atoi(rtctool_args.arg1->sval[0]);
        if (tz < -12 || tz > 14) {
            printf("Timezone offset must be -12..14 hours\n");
            return 1;
        }

        ClockSettings settings = rtc_load_settings();
        settings.tz_offset_hours = static_cast<int8_t>(tz);
        g_system_controller->request_settings_update(settings, nullptr);
        printf("Timezone offset set to %+d h\n", settings.tz_offset_hours);
        return 0;
    }

    printf("Unknown subcommand: %s\n", subcmd);
    printf("Usage: rtctool <read|set_time|set_alarm|set_tz> [args...]\n");
    return 1;
}

// --- Command: get_hw_version ---
static int get_hw_version_func(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("HW Version: ESP32-S3 (Rev %d)\nBoard version: %s\nDisplay board: %s\n",
           chip_info.revision, DEV_BOARD_VERSION,
           display_board_type_name(get_display_board_type()));
    return 0;
}

// --- Command: get_fw_version ---
static int get_fw_version_func(int argc, char **argv)
{
    printf("App Version: %s\n", GIT_COMMIT_HASH);
    printf("IDF FW Version: %s\n", esp_get_idf_version());
    return 0;
}

// --- Command: get_fw_version ---
static int reboot_func(int argc, char **argv)
{
    printf("Reboot the device");
    esp_restart();
    return 0;
}

// --- Command: get_bq25601_status ---
static int get_bq25601_status_func(int argc, char **argv)
{
    if (!g_charger_controller) {
        printf("charger controller not ready\n");
        return 1;
    }

    uint8_t status = 0;
    if (!g_charger_controller->read_status_register(status)) {
        printf("Failed to read BQ25601 status register (REG08)\n");
        return 1;
    }

    uint8_t reg01 = 0;
    if (!g_charger_controller->read_power_on_config_register(reg01)) {
        printf("Failed to read BQ25601 power-on config register (REG01)\n");
        return 1;
    }

    printf("BQ25601 REG08 (System Status): 0x%02X\n", status);
    printf("BQ25601 REG01 (Power-on Config): 0x%02X\n", reg01);
    return 0;
}

// --- Command: enable_charging ---
static int enable_charging_func(int argc, char **argv)
{
    if (!g_charger_controller) {
        printf("charger controller not ready\n");
        return 1;
    }
    bool ok = g_charger_controller->enable_charging();
    printf(ok ? "Charging enabled\n" : "Failed to enable charging\n");
    return ok ? 0 : 1;
}

// --- Command: disable_charging ---
static int disable_charging_func(int argc, char **argv)
{
    if (!g_charger_controller) {
        printf("charger controller not ready\n");
        return 1;
    }
    bool ok = g_charger_controller->disable_charging();
    printf(ok ? "Charging disabled\n" : "Failed to disable charging\n");
    return ok ? 0 : 1;
}

// --- Command: enable_hv ---
static int enable_hv_func(int argc, char **argv)
{
    if (!g_power_controller) {
        printf("power controller not ready\n");
        return 1;
    }
    g_power_controller->set_hv_enabled(true);
    printf("HV rail enabled\n");
    return 0;
}

// --- Command: disable_hv ---
static int disable_hv_func(int argc, char **argv)
{
    if (!g_power_controller) {
        printf("power controller not ready\n");
        return 1;
    }
    g_power_controller->set_hv_enabled(false);
    printf("HV rail disabled\n");
    return 0;
}

// --- Command: enable_df_power ---
static int enable_df_power_func(int argc, char **argv)
{
    if (!g_power_controller) {
        printf("power controller not ready\n");
        return 1;
    }
    g_power_controller->set_dfplayer_enabled(true);
    printf("DFPlayer power enabled\n");
    return 0;
}

// --- Command: disable_df_power ---
static int disable_df_power_func(int argc, char **argv)
{
    if (!g_power_controller) {
        printf("power controller not ready\n");
        return 1;
    }
    g_power_controller->set_dfplayer_enabled(false);
    printf("DFPlayer power disabled\n");
    return 0;
}

// --- Command: help ---
static int help_func(int argc, char **argv)
{
    // TODO: complete the help dialog
    printf("============= NIXIE TUBE CLOCK CLI V0.9.0 ============================\n");
    printf("help                                            Show this help message\n");
    printf("set_backlight --rgb <r,g,b> --brightness <int>  Set LED backlight color and brightness\n");
    printf("set_nixie --number <123456>                     Set nixie digit number, 6 digits\n");
    printf("get_uuid                                        Get UUID of device\n");
    printf("get_hw_version                                  Get hardware version\n");
    printf("get_fw_version                                  Get firmware version\n");
    printf("get_bq25601_status                             Get BQ25601 status register (REG08, REG01)\n");
    printf("enable_charging                                 Enable charging\n");
    printf("disable_charging                                Disable charging\n");
    printf("enable_hv                                       Enable HV power rail\n");
    printf("disable_hv                                      Disable HV power rail\n");
    printf("enable_df_power                                 Enable DFPlayer power rail\n");
    printf("disable_df_power                                Disable DFPlayer power rail\n");
    printf("dftool list                                     List mp3/ folder tracks on SD card\n");
    printf("dftool status                                   Query DFPlayer playback status\n");
    printf("dftool play <track> [--loop]                    Play a track (optional loop)\n");
    printf("dftool toggle <track>                           Play/pause toggle (web UI behavior)\n");
    printf("dftool pause|resume|stop                        Pause, resume, or stop playback\n");
    printf("dftool next|prev                                Play next or previous track\n");
    printf("dftool volume <0-30>                            Set volume\n");
    printf("dftool vol_up|vol_down                          Step volume up or down\n");
    printf("ggtool <status|peek|block|config|read|cache>  BQ27441 gasgauge tools\n");
    printf("rtctool read                                    Read RTC time and alarm status\n");
    printf("rtctool set_time <YYYY-MM-DD HH:MM:SS>          Calibrate RTC (local wall clock)\n");
    printf("rtctool set_alarm <HH:MM:SS> [--enable|--disable] [--track <n>]\n");
    printf("                                                Set daily alarm (local time)\n");
    printf("rtctool set_tz <offset_hours>                   Set timezone offset (-12..14)\n");
    printf("reboot                                          reboot the device\n");
    return 0;
}

CliDaemon::CliDaemon(SystemController &system_controller,
                     ChargerController &charger_controller,
                     GasgaugeService &gasgauge_service,
                     PowerController &power_controller,
                     SystemState &system_state,
                     AudioDaemon &audio_daemon)
    : system_controller_(system_controller),
      charger_controller_(charger_controller),
      power_controller_(power_controller),
      gasgauge_service_(gasgauge_service),
      system_state_(system_state),
      audio_daemon_(audio_daemon),
      task_handle_(nullptr)
{
    g_system_controller = &system_controller;
    g_charger_controller = &charger_controller;
    g_gasgauge_service = &gasgauge_service;
    g_power_controller = &power_controller;
    g_system_state = &system_state;
    g_audio_daemon = &audio_daemon;
}

CliDaemon::~CliDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void CliDaemon::start()
{
    xTaskCreate(task_entry, "cli_daemon", 4096, this, 5, &task_handle_);
}

void CliDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<CliDaemon *>(param);
    daemon->loop();
}

void CliDaemon::register_commands()
{
    esp_console_config_t console_config = {};
    console_config.max_cmdline_length = 256;
    console_config.max_cmdline_args = 8;
    console_config.hint_color = 37;
    console_config.hint_bold = 0;
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // Configure UART
    uart_config_t uart_config = {};
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    
    // Initialize VFS
    uart_vfs_dev_use_driver(UART_NUM_0);
    uart_vfs_dev_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
    
    // Initialize linenoise
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(100);

    // Register: set_nixie
    nixie_args.number = arg_int1(NULL, "number", "<n>", "Number to display");
    nixie_args.end = arg_end(20);
    const esp_console_cmd_t set_nixie_cmd = {
        .command = "set_nixie",
        .help = "Set Nixie Tube Number",
        .hint = NULL,
        .func = &set_nixie_func,
        .argtable = &nixie_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_nixie_cmd));

    // Register: set_backlight
    backlight_args.rgb = arg_str0(NULL, "rgb", "<r,g,b>", "RGB Color");
    backlight_args.brightness = arg_int0(NULL, "brightness", "<b>", "Brightness");
    backlight_args.end = arg_end(20);
    const esp_console_cmd_t set_backlight_cmd = {
        .command = "set_backlight",
        .help = "Set Backlight Color and Brightness",
        .hint = NULL,
        .func = &set_backlight_func,
        .argtable = &backlight_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_backlight_cmd));

    // Register: ggtool
    ggtool_args.subcmd = arg_str1(NULL, NULL, "<subcmd>", "status|peek|block|config|read|cache");
    ggtool_args.arg1 = arg_str0(NULL, NULL, "<arg>", "Subcommand argument");
    ggtool_args.arg2 = arg_str0(NULL, NULL, "<arg>", "Subcommand argument");
    ggtool_args.end = arg_end(4);
    const esp_console_cmd_t ggtool_cmd = {
        .command = "ggtool",
        .help = "BQ27441 gasgauge: status, peek, block, config, read, cache",
        .hint = NULL,
        .func = &ggtool_func,
        .argtable = &ggtool_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ggtool_cmd));

    // Register: rtctool
    rtctool_args.subcmd = arg_str1(NULL, NULL, "<subcmd>", "read|set_time|set_alarm|set_tz");
    rtctool_args.arg1 = arg_str0(NULL, NULL, "<arg>", "Subcommand argument");
    rtctool_args.arg2 = arg_str0(NULL, NULL, "<arg>", "Subcommand argument or option");
    rtctool_args.arg3 = arg_str0(NULL, NULL, "<opt>", "Subcommand option");
    rtctool_args.arg4 = arg_str0(NULL, NULL, "<opt>", "Subcommand option value");
    rtctool_args.end = arg_end(4);
    const esp_console_cmd_t rtctool_cmd = {
        .command = "rtctool",
        .help = "DS3231 RTC: read, set_time, set_alarm, set_tz",
        .hint = NULL,
        .func = &rtctool_func,
        .argtable = &rtctool_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&rtctool_cmd));

    // Register: get_uuid
    const esp_console_cmd_t get_uuid_cmd = {
        .command = "get_uuid",
        .help = "Get Device UUID",
        .hint = NULL,
        .func = &get_uuid_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_uuid_cmd));

    // Register: get_hw_version
    const esp_console_cmd_t get_hw_version_cmd = {
        .command = "get_hw_version",
        .help = "Get Hardware Version",
        .hint = NULL,
        .func = &get_hw_version_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_hw_version_cmd));

    // Register: get_fw_version
    const esp_console_cmd_t get_fw_version_cmd = {
        .command = "get_fw_version",
        .help = "Get Firmware Version",
        .hint = NULL,
        .func = &get_fw_version_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_fw_version_cmd));

    // Register: reboot
    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Reboot the device",
        .hint = NULL,
        .func = &reboot_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));

    // Register: get_bq25601_status
    const esp_console_cmd_t get_bq25601_status_cmd = {
        .command = "get_bq25601_status",
        .help = "Get BQ25601 status register (REG08, REG01)",
        .hint = NULL,
        .func = &get_bq25601_status_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_bq25601_status_cmd));

    // Register: enable_charging
    const esp_console_cmd_t enable_charging_cmd = {
        .command = "enable_charging",
        .help = "Enable charging",
        .hint = NULL,
        .func = &enable_charging_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&enable_charging_cmd));

    // Register: disable_charging
    const esp_console_cmd_t disable_charging_cmd = {
        .command = "disable_charging",
        .help = "Disable charging",
        .hint = NULL,
        .func = &disable_charging_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&disable_charging_cmd));

    // Register: enable_hv
    const esp_console_cmd_t enable_hv_cmd = {
        .command = "enable_hv",
        .help = "Enable HV power rail",
        .hint = NULL,
        .func = &enable_hv_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&enable_hv_cmd));

    // Register: disable_hv
    const esp_console_cmd_t disable_hv_cmd = {
        .command = "disable_hv",
        .help = "Disable HV power rail",
        .hint = NULL,
        .func = &disable_hv_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&disable_hv_cmd));

    // Register: enable_df_power
    const esp_console_cmd_t enable_df_power_cmd = {
        .command = "enable_df_power",
        .help = "Enable DFPlayer power rail",
        .hint = NULL,
        .func = &enable_df_power_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&enable_df_power_cmd));

    // Register: disable_df_power
    const esp_console_cmd_t disable_df_power_cmd = {
        .command = "disable_df_power",
        .help = "Disable DFPlayer power rail",
        .hint = NULL,
        .func = &disable_df_power_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&disable_df_power_cmd));

    // Register: dftool
    dftool_args.subcmd = arg_str1(NULL, NULL, "<subcmd>",
                                  "list|status|play|toggle|pause|resume|stop|next|prev|volume|vol_up|vol_down");
    dftool_args.arg1 = arg_str0(NULL, NULL, "<arg>", "Subcommand argument (track or volume)");
    dftool_args.arg2 = arg_str0(NULL, NULL, "<opt>", "Optional flag (e.g. --loop)");
    dftool_args.end = arg_end(4);
    const esp_console_cmd_t dftool_cmd = {
        .command = "dftool",
        .help = "DFPlayer: list, status, play, toggle, pause, resume, stop, next, prev, volume",
        .hint = NULL,
        .func = &dftool_func,
        .argtable = &dftool_args,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&dftool_cmd));

    // Register: help
    const esp_console_cmd_t help_cmd = {
        .command = "help",
        .help = "Print this help information",
        .hint = NULL,
        .func = &help_func,
        .argtable = NULL,
        .func_w_context = nullptr,
        .context = nullptr,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&help_cmd));
}

void CliDaemon::loop()
{
    register_commands();
    
    // Remove welcome message to not interference debug output
    // printf("\n"
    //        "Welcome to Nixie Clock CLI\n"
    //        "Type 'help' to get the list of commands.\n"
    //        "\n");

    while (true) {
        char *line = linenoise("nixie_clock> ");
        if (line == NULL) {
            break;
        }
        
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
            int ret;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND) {
                printf("Unrecognized command\n");
            } else if (err == ESP_ERR_INVALID_ARG) {
                // command was empty
            } else if (err == ESP_OK && ret != ESP_OK) {
                printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            } else if (err != ESP_OK) {
                printf("Internal error: %s\n", esp_err_to_name(err));
            }
        }
        
        linenoiseFree(line);
    }
}