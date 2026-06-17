#include "daemons/cli_daemon.h"
#include "gasgauge_service.h"
#include "bq27441/bq27441_regs.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "esp_mac.h"
#include <cstring>
#include <cstdio>

static const char *TAG = "CliDaemon";
static SystemController *g_system_controller = nullptr;
static ChargerController *g_charger_controller = nullptr;
static PowerController *g_power_controller = nullptr;
static GasgaugeService *g_gasgauge_service = nullptr;
static SystemState *g_system_state = nullptr;

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "unknown"
#endif

#define DEV_BOARD_VERSION "v0.9.0" // Hardcoded for now

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
        if (argc < 3) {
            printf("Usage: ggtool peek <reg_hex> [len]\n");
            return 1;
        }

        const uint8_t reg = static_cast<uint8_t>(strtoul(argv[2], nullptr, 16));
        const size_t len = (argc >= 4) ? static_cast<size_t>(strtoul(argv[3], nullptr, 10)) : 1U;
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
        const uint8_t class_id = (argc >= 3)
            ? static_cast<uint8_t>(strtoul(argv[2], nullptr, 0))
            : static_cast<uint8_t>(82);
        const uint8_t block_index = (argc >= 4)
            ? static_cast<uint8_t>(strtoul(argv[3], nullptr, 0))
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
        if (argc >= 3) {
            mah = static_cast<uint16_t>(strtoul(argv[2], nullptr, 10));
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

// --- Command: get_hw_version ---
static int get_hw_version_func(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("HW Version: ESP32-S3 (Rev %d)\nBoard version: %s\n", chip_info.revision, DEV_BOARD_VERSION);
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
    printf("ggtool <status|peek|block|config|read|cache>  BQ27441 gasgauge tools\n");
    printf("reboot                                          reboot the device\n");
    return 0;
}

CliDaemon::CliDaemon(SystemController &system_controller,
                     ChargerController &charger_controller,
                     GasgaugeService &gasgauge_service,
                     PowerController &power_controller,
                     SystemState &system_state)
    : system_controller_(system_controller),
      charger_controller_(charger_controller),
      power_controller_(power_controller),
      gasgauge_service_(gasgauge_service),
      system_state_(system_state),
      task_handle_(nullptr)
{
    g_system_controller = &system_controller;
    g_charger_controller = &charger_controller;
    g_gasgauge_service = &gasgauge_service;
    g_power_controller = &power_controller;
    g_system_state = &system_state;
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
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
    esp_vfs_dev_uart_port_set_rx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CR);
    esp_vfs_dev_uart_port_set_tx_line_endings(UART_NUM_0, ESP_LINE_ENDINGS_CRLF);
    
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