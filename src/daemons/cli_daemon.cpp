#include "daemons/cli_daemon.h"
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
    struct arg_int *rgb;
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
        msg.data.cli.backlight.has_color = true;
        msg.data.cli.backlight.r = backlight_args.rgb->ival[0];
        msg.data.cli.backlight.g = backlight_args.rgb->ival[1];
        msg.data.cli.backlight.b = backlight_args.rgb->ival[2];
    }

    if (backlight_args.brightness->count > 0) {
        msg.data.cli.backlight.has_brightness = true;
        msg.data.cli.backlight.brightness = backlight_args.brightness->ival[0];
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

// --- Command: get_hw_version ---
static int get_hw_version_func(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("HW Version: ESP32-S3 (Rev %d), Board: %s\n", chip_info.revision, DEV_BOARD_VERSION);
    return 0;
}

// --- Command: get_fw_version ---
static int get_fw_version_func(int argc, char **argv)
{
    printf("App Version: %s\n", GIT_COMMIT_HASH);
    printf("IDF Version: %s\n", esp_get_idf_version());
    return 0;
}

CliDaemon::CliDaemon(SystemController &system_controller)
    : system_controller_(system_controller), task_handle_(nullptr)
{
    g_system_controller = &system_controller;
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
    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
        .hint_color = 37,
        .hint_bold = 0
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
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
        .argtable = &nixie_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_nixie_cmd));

    // Register: set_backlight
    backlight_args.rgb = arg_intn(NULL, "rgb", "<r,g,b>", 0, 3, "RGB Color");
    backlight_args.brightness = arg_int0(NULL, "brightness", "<b>", "Brightness");
    backlight_args.end = arg_end(20);
    const esp_console_cmd_t set_backlight_cmd = {
        .command = "set_backlight",
        .help = "Set Backlight Color and Brightness",
        .hint = NULL,
        .func = &set_backlight_func,
        .argtable = &backlight_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_backlight_cmd));

    // Register: get_uuid
    const esp_console_cmd_t get_uuid_cmd = {
        .command = "get_uuid",
        .help = "Get Device UUID",
        .hint = NULL,
        .func = &get_uuid_func,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_uuid_cmd));

    // Register: get_hw_version
    const esp_console_cmd_t get_hw_version_cmd = {
        .command = "get_hw_version",
        .help = "Get Hardware Version",
        .hint = NULL,
        .func = &get_hw_version_func,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_hw_version_cmd));

    // Register: get_fw_version
    const esp_console_cmd_t get_fw_version_cmd = {
        .command = "get_fw_version",
        .help = "Get Firmware Version",
        .hint = NULL,
        .func = &get_fw_version_func,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_fw_version_cmd));
}

void CliDaemon::loop()
{
    register_commands();
    
    printf("\n"
           "Welcome to Nixie Clock CLI\n"
           "Type 'help' to get the list of commands.\n"
           "\n");

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