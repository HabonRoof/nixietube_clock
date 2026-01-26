#include "system_controller.h"
#include "esp_log.h"
#include <ctime>
#include "settings_store.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "SystemController";

// Hardware Configuration
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSda = static_cast<gpio_num_t>(6);
constexpr gpio_num_t kI2cScl = static_cast<gpio_num_t>(5);
constexpr uint32_t kI2cClockHz = 400000;

constexpr uart_port_t kUartPort = UART_NUM_1;
constexpr gpio_num_t kUartTx = static_cast<gpio_num_t>(18);
constexpr gpio_num_t kUartRx = static_cast<gpio_num_t>(17);
constexpr int kUartBaudRate = 9600;

constexpr gpio_num_t kRtcIntPin = static_cast<gpio_num_t>(8);
constexpr gpio_num_t kPca9685OePin = static_cast<gpio_num_t>(4);
constexpr gpio_num_t kLedDataInPin = static_cast<gpio_num_t>(7);
constexpr uint32_t kRmtResolutionHz = 40000000; // 25ns resolution

HardwareHandles SystemController::init_hardware()
{
    ESP_LOGI(TAG, "Initializing Hardware...");
    HardwareHandles handles = {};

    // 1. Initialize I2C
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = kI2cSda;
    i2c_conf.scl_io_num = kI2cScl;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = kI2cClockHz;
    ESP_ERROR_CHECK(i2c_param_config(kI2cPort, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(kI2cPort, i2c_conf.mode, 0, 0, 0));
    handles.i2c_port = kI2cPort;
    ESP_LOGI(TAG, "I2C Initialized");

    // 2. Initialize UART
    uart_config_t uart_config = {
        .baud_rate = kUartBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(kUartPort, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(kUartPort, kUartTx, kUartRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(kUartPort, 256, 0, 0, nullptr, 0));
    handles.audio_uart_port = kUartPort;
    ESP_LOGI(TAG, "UART Initialized");

    // 3. Initialize RTC Interrupt Pin
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE; // RTC INT is active low
    io_conf.pin_bit_mask = (1ULL << kRtcIntPin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_LOGI(TAG, "RTC Interrupt Pin Initialized");

    // 4. Initialize PCA9685 OE Pin
    gpio_config_t oe_conf = {};
    oe_conf.intr_type = GPIO_INTR_DISABLE;
    oe_conf.mode = GPIO_MODE_OUTPUT;
    oe_conf.pin_bit_mask = (1ULL << kPca9685OePin);
    oe_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    oe_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Pull up to disable by default
    ESP_ERROR_CHECK(gpio_config(&oe_conf));
    ESP_ERROR_CHECK(gpio_set_level(kPca9685OePin, 1)); // Disable output (Active LOW)
    ESP_LOGI(TAG, "PCA9685 OE Pin Initialized (Disabled)");

    // 5. Initialize RMT for WS2812
    ESP_LOGI(TAG, "Initializing RMT for WS2812...");
    rmt_tx_channel_config_t rmt_config = {
        .gpio_num = kLedDataInPin,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = kRmtResolutionHz,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {
            .invert_out = 0,
            .with_dma = 0,
            .io_loop_back = 0,
            .io_od_mode = 0,
            .allow_pd = 0,
        },
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&rmt_config, &handles.led_rmt_channel));

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoder_config, &handles.led_rmt_encoder));
    
    ESP_ERROR_CHECK(rmt_enable(handles.led_rmt_channel));
    ESP_LOGI(TAG, "RMT Initialized");


    ESP_LOGI(TAG, "Hardware Initialization Process Done!");
    return handles;
}

SystemController::SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon)
    : display_daemon_(display_daemon),
      audio_daemon_(audio_daemon),
      queue_(nullptr),
      task_handle_(nullptr),
      rtc_(kI2cPort),
      settings_(SettingsStore::defaults())
{
    queue_ = xQueueCreate(10, sizeof(SystemMessage));
    
    if (rtc_.init()) {
        ESP_LOGI(TAG, "RTC Initialized");
        // Optional: Sync system time from RTC here
    } else {
        ESP_LOGE(TAG, "RTC Initialization Failed");
    }
}

SystemController::~SystemController()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (queue_) {
        vQueueDelete(queue_);
    }
}

void SystemController::start()
{
    xTaskCreate(task_entry, "system_controller", 4096, this, 5, &task_handle_);
}

QueueHandle_t SystemController::get_queue() const
{
    return queue_;
}

void SystemController::task_entry(void *param)
{
    auto *controller = static_cast<SystemController *>(param);
    controller->loop();
}

void SystemController::loop()
{
    ESP_LOGI(TAG, "System Controller Started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update time every second

    while (true) {
        SystemMessage msg;
        // Check for system events (buttons, etc.)
        if (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
            process_message(msg);
        }

        // Periodic tasks
        update_time();

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}

void SystemController::process_message(const SystemMessage &msg)
{
    switch (msg.event) {
        case SystemEvent::BUTTON_PRESSED:
            ESP_LOGI(TAG, "Button pressed: %d", msg.data.button_id);
            // Example: Toggle effect on button press
            // DisplayMessage dmsg;
            // dmsg.command = DisplayCmd::SET_EFFECT;
            // ...
            // xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            break;
        case SystemEvent::CLI_COMMAND:
            if (msg.data.cli.type == CliCommandType::SET_NIXIE) {
                DisplayMessage dmsg;
                dmsg.command = DisplayCmd::SET_MODE;
                dmsg.data.mode = DisplayMode::MANUAL_DISPLAY;
                xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

                dmsg.command = DisplayCmd::SET_MANUAL_NUMBER;
                dmsg.data.number = msg.data.cli.value;
                xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            } else if (msg.data.cli.type == CliCommandType::SET_BACKLIGHT) {
                if (msg.data.cli.backlight.has_color) {
                    DisplayMessage dmsg;
                    dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
                    dmsg.data.color.r = msg.data.cli.backlight.r;
                    dmsg.data.color.g = msg.data.cli.backlight.g;
                    dmsg.data.color.b = msg.data.cli.backlight.b;
                    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
                }
                if (msg.data.cli.backlight.has_brightness) {
                    DisplayMessage dmsg;
                    dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
                    dmsg.data.brightness = msg.data.cli.backlight.brightness;
                    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
                }
            }
            break;
        case SystemEvent::BATTERY_UPDATE:
            {
                DisplayMessage dmsg;
                dmsg.command = DisplayCmd::UPDATE_BATTERY;
                dmsg.data.battery = msg.data.battery;
                xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            }
            break;
        default:
            break;
    }
}

void SystemController::apply_settings(const ClockSettings &settings, const struct tm *new_time)
{
    settings_ = settings;

    if (new_time) {
        struct tm adjusted = *new_time;
        adjusted.tm_isdst = 0;
        time_t epoch = mktime(&adjusted);
        if (epoch != -1) {
            localtime_r(&epoch, &adjusted);
        }
        rtc_.set_time(&adjusted);
    }

    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
    dmsg.data.color.r = settings.backlight_r;
    dmsg.data.color.g = settings.backlight_g;
    dmsg.data.color.b = settings.backlight_b;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
    dmsg.data.brightness = settings.backlight_brightness;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    AudioMessage amsg = {};
    amsg.command = AudioCmd::SET_VOLUME;
    amsg.param.volume = settings.volume;
    xQueueSend(audio_daemon_.get_queue(), &amsg, 0);

    if (settings.alarm_enabled) {
        struct tm alarm = {};
        alarm.tm_hour = settings.alarm_hour;
        alarm.tm_min = settings.alarm_minute;
        alarm.tm_sec = settings.alarm_second;
        alarm.tm_mday = 1;
        rtc_.set_alarm1(&alarm);
        rtc_.clear_alarm1_flag();
        rtc_.enable_alarm1_interrupt(true);
    } else {
        rtc_.enable_alarm1_interrupt(false);
        rtc_.clear_alarm1_flag();
    }
}

void SystemController::update_time()
{
    // Get current time from RTC
    struct tm timeinfo;
    if (rtc_.get_time(&timeinfo)) {
        // Send time update to Display Daemon
        DisplayMessage msg;
        msg.command = DisplayCmd::UPDATE_TIME;
        msg.data.time.h = timeinfo.tm_hour;
        msg.data.time.m = timeinfo.tm_min;
        msg.data.time.s = timeinfo.tm_sec;
        
        xQueueSend(display_daemon_.get_queue(), &msg, 0);
    } else {
        ESP_LOGW(TAG, "Failed to read time from RTC");
    }
    
    /*
    // Fallback or original logic if needed
    time_t now;
    time(&now);
    struct tm timeinfo_sys;
    localtime_r(&now, &timeinfo_sys);
    */
}
