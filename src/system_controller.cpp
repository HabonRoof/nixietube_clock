#include "system_controller.h"
#include "button_config.h"
#include "esp_log.h"
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include "system_state.h"
#include "gasgauge_service.h"
#include "charger_controller.h"
#include "i2c_debug_config.h"
#include "wifi_credentials.h"
#include "i2c_bus.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "display_board_config.h"
#include "esp_timer.h"

static const char *TAG = "SystemController";

namespace {

bool in_hibernate_period(uint16_t now_minutes, const HibernationSettings &hibernation)
{
    const uint16_t start =
        static_cast<uint16_t>(hibernation.start_hour) * 60U + hibernation.start_minute;
    const uint16_t end =
        static_cast<uint16_t>(hibernation.end_hour) * 60U + hibernation.end_minute;
    if (start == end) {
        return false;
    }
    if (start < end) {
        return now_minutes >= start && now_minutes < end;
    }
    return now_minutes >= start || now_minutes < end;
}

bool hibernate_is_active(const ClockSettings &settings, uint8_t hour, uint8_t minute)
{
    if (!settings.hibernation.enabled) {
        return false;
    }
    const uint16_t now_minutes = static_cast<uint16_t>(hour) * 60U + minute;
    return in_hibernate_period(now_minutes, settings.hibernation);
}

} // namespace

// Timekeeping policy: maintain ESP32 system time (UTC) and periodically
// resync from the DS3231 (the ±2ppm truth source) to correct drift.
static constexpr uint32_t kResyncIntervalMs = 3600000; // 1 hour
static constexpr uint8_t kMaxReadFailures = 5;

// Convert a broken-down UTC time to a Unix epoch without relying on timegm()
// (not exposed by the default ESP-IDF newlib config). Days-from-civil per
// Howard Hinnant's algorithm.
static time_t tm_to_utc_epoch(const struct tm *t)
{
    int year = t->tm_year + 1900;
    unsigned month = static_cast<unsigned>(t->tm_mon + 1);
    unsigned day = static_cast<unsigned>(t->tm_mday);

    year -= (month <= 2);
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = static_cast<unsigned>(year - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;

    return static_cast<time_t>(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
}

// Parse the firmware build timestamp (__DATE__ "Jun 17 2026", __TIME__
// "19:10:00") into a broken-down time. Treated as UTC; this is only a
// placeholder used to clear the OSF when running without a VBAT battery.
static void build_time_to_tm(struct tm *out)
{
    static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char month_str[4] = {0};
    int day = 1, year = 2025, hour = 0, min = 0, sec = 0;
    sscanf(__DATE__, "%3s %d %d", month_str, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);

    int mon = 0;
    for (int i = 0; i < 12; ++i) {
        if (strncmp(month_str, kMonths + i * 3, 3) == 0) {
            mon = i;
            break;
        }
    }

    *out = {};
    out->tm_year = year - 1900;
    out->tm_mon = mon;
    out->tm_mday = day;
    out->tm_hour = hour;
    out->tm_min = min;
    out->tm_sec = sec;
    out->tm_isdst = 0;
}

// Hardware Configuration — two independent I2C buses (main_board J3 pinout).
constexpr i2c_port_t kI2c0Port = I2C_NUM_0;
constexpr gpio_num_t kI2c0Sda = GPIO_NUM_6;
constexpr gpio_num_t kI2c0Scl = GPIO_NUM_5;
constexpr i2c_port_t kI2c1Port = I2C_NUM_1;
constexpr gpio_num_t kI2c1Sda = GPIO_NUM_18;
constexpr gpio_num_t kI2c1Scl = GPIO_NUM_17;
constexpr uint32_t kI2cClockHz = 400000;

void init_i2c_master(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = sda;
    i2c_conf.scl_io_num = scl;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = kI2cClockHz;
    ESP_ERROR_CHECK(i2c_param_config(port, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, i2c_conf.mode, 0, 0, 0));
    i2c_bus_init(port);
}

constexpr uart_port_t kUartPort = UART_NUM_1;
constexpr gpio_num_t kUartTx = static_cast<gpio_num_t>(42);
constexpr gpio_num_t kUartRx = static_cast<gpio_num_t>(41);
constexpr int kUartBaudRate = 9600;

constexpr gpio_num_t kRtcIntPin = static_cast<gpio_num_t>(2);
constexpr gpio_num_t kPca9685OePin = static_cast<gpio_num_t>(4);
constexpr gpio_num_t kAnodeA0 = static_cast<gpio_num_t>(9);
constexpr gpio_num_t kAnodeA1 = static_cast<gpio_num_t>(10);
constexpr gpio_num_t kAnodeA2 = static_cast<gpio_num_t>(11);
constexpr gpio_num_t kLedDataInPin = static_cast<gpio_num_t>(7);
constexpr uint32_t kRmtResolutionHz = 40000000; // 25ns resolution

HardwareHandles SystemController::init_hardware()
{
    ESP_LOGI(TAG, "Initializing Hardware...");
    HardwareHandles handles = {};

    // 1. Initialize I2C buses (I2C0: main board, I2C1: display board — DS3231 + LTR-303)
    init_i2c_master(kI2c0Port, kI2c0Sda, kI2c0Scl);
    init_i2c_master(kI2c1Port, kI2c1Sda, kI2c1Scl);
    handles.i2c0_port = kI2c0Port;
    handles.i2c1_port = kI2c1Port;
    ESP_LOGI(TAG, "I2C0 (GPIO%d/%d) and I2C1 (GPIO%d/%d) initialized",
             kI2c0Sda, kI2c0Scl, kI2c1Sda, kI2c1Scl);

    // 2. Initialize UART
    uart_config_t uart_config = {};
    uart_config.baud_rate = kUartBaudRate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
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

    // 4. Read upper display board type (GPIO19 / DISPLAY_TYPE) before MUX / PCA9685 init.
    init_display_type_adc();
    handles.display_board_type = get_display_board_type();
    ESP_LOGI(TAG, "Display Board Type: %s", display_board_type_name(handles.display_board_type));

    // 5. Initialize PCA9685 OE Pin
    gpio_config_t oe_conf = {};
    oe_conf.intr_type = GPIO_INTR_DISABLE;
    oe_conf.mode = GPIO_MODE_OUTPUT;
    oe_conf.pin_bit_mask = (1ULL << kPca9685OePin);
    oe_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    oe_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Pull up to disable by default
    ESP_ERROR_CHECK(gpio_config(&oe_conf));
    ESP_ERROR_CHECK(gpio_set_level(kPca9685OePin, 1)); // Disable output (Active LOW)
    ESP_LOGI(TAG, "PCA9685 OE Pin Initialized (Disabled)");

    // 6. Initialize 74HC238 anode mux (blank until scan task starts)
    gpio_config_t anode_conf = {};
    anode_conf.intr_type = GPIO_INTR_DISABLE;
    anode_conf.mode = GPIO_MODE_OUTPUT;
    anode_conf.pin_bit_mask = (1ULL << kAnodeA0) | (1ULL << kAnodeA1) | (1ULL << kAnodeA2);
    anode_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    anode_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&anode_conf));
    ESP_ERROR_CHECK(gpio_set_level(kAnodeA0, 1));
    ESP_ERROR_CHECK(gpio_set_level(kAnodeA1, 1));
    ESP_ERROR_CHECK(gpio_set_level(kAnodeA2, 1));
    ESP_LOGI(TAG, "74HC238 Anode Mux Initialized (Blank)");

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

SystemController::SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon,
                                 SystemState &system_state,
                                 GasgaugeService *gasgauge_service,
                                 bool gasgauge_ready_at_boot,
                                 ChargerController *charger_controller)
    : display_daemon_(display_daemon),
      audio_daemon_(audio_daemon),
      system_state_(system_state),
      gasgauge_service_(gasgauge_service),
      charger_controller_(charger_controller),
      queue_(nullptr),
      task_handle_(nullptr),
      rtc_(kI2c1Port),
      rtc_read_failures_(0),
      battery_read_failures_(0),
      gasgauge_ready_(gasgauge_ready_at_boot),
      alarm_audio_active_(false),
      alarm_stop_timer_(nullptr),
      display_preview_timer_(nullptr),
      hibernate_state_(HibernateState::Normal),
      hibernate_window_active_(false),
      hibernation_peek_deadline_(0),
      current_display_mode_(DisplayMode::CLOCK_HHMMSS),
      next_rtc_sync_(0),
      next_battery_poll_(0),
      standby_active_(false),
      idle_standby_deadline_(0),
      next_auto_cathode_(0),
      display_preview_active_(false),
      display_preview_{},
      wifi_config_ui_active_(false),
      wifi_config_phase_(WifiConfigPhase::WaitingClient),
      display_mode_before_wifi_config_(DisplayMode::CLOCK_HHMMSS),
      hibernate_state_before_wifi_config_(HibernateState::Normal),
      battery_protection_enabled_(false),
      battery_protection_charging_paused_(false)
{
    queue_ = xQueueCreate(32, sizeof(SystemMessage));
    
    // TODO: Remove this once the DS3231 RTC is working
    if (i2c_debug::kDisableDs3231Rtc) {
        ESP_LOGW(TAG, "DS3231 RTC I2C disabled");
    } else if (rtc_.init()) {
        ESP_LOGI(TAG, "RTC Initialized");
    } else {
        ESP_LOGE(TAG, "RTC Initialization Failed");
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = alarm_stop_timer_cb;
    timer_args.arg = this;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "alarm_stop";
    if (esp_timer_create(&timer_args, &alarm_stop_timer_) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create alarm stop timer");
        alarm_stop_timer_ = nullptr;
    }

    esp_timer_create_args_t preview_timer_args = {};
    preview_timer_args.callback = display_preview_timer_cb;
    preview_timer_args.arg = this;
    preview_timer_args.dispatch_method = ESP_TIMER_TASK;
    preview_timer_args.name = "disp_preview";
    if (esp_timer_create(&preview_timer_args, &display_preview_timer_) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create display preview timer");
        display_preview_timer_ = nullptr;
    }
}

SystemController::~SystemController()
{
    cancel_alarm_timer();
    if (alarm_stop_timer_) {
        esp_timer_delete(alarm_stop_timer_);
        alarm_stop_timer_ = nullptr;
    }
    stop_display_preview_timer();
    if (display_preview_timer_) {
        esp_timer_delete(display_preview_timer_);
        display_preview_timer_ = nullptr;
    }
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

    const TickType_t poll_interval = pdMS_TO_TICKS(20);
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update time every second
    TickType_t next_time_update = xTaskGetTickCount() + update_interval;

    // Initial sync: seed ESP32 system time from the DS3231 (UTC).
    sync_time_from_rtc();
    next_rtc_sync_ = xTaskGetTickCount() + pdMS_TO_TICKS(kResyncIntervalMs);
    idle_standby_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kIdleStandbyMs);
    next_auto_cathode_ = xTaskGetTickCount() + pdMS_TO_TICKS(kAutoCathodeIntervalMs);
    if (gasgauge_service_ && !i2c_debug::kDisableGasgauge) {
        next_battery_poll_ = xTaskGetTickCount() + pdMS_TO_TICKS(i2c_debug::kBatteryPollMs);
    }

    while (true) {
        SystemMessage msg;
        // Drain all pending messages so producers won't overflow the queue.
        while (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
            process_message(msg);
        }

        TickType_t now = xTaskGetTickCount();

        // Periodically correct system-time drift from the DS3231.
        if ((int32_t)(now - next_rtc_sync_) >= 0) {
            sync_time_from_rtc();
            next_rtc_sync_ = now + pdMS_TO_TICKS(kResyncIntervalMs);
        }

        if (gasgauge_service_ && !i2c_debug::kDisableGasgauge &&
            (int32_t)(now - next_battery_poll_) >= 0) {
            sync_battery_from_gauge();
            // TODO: Add webUI hookup after webui merge
            // Need to save the setting to NVS in ClockSettings
            apply_battery_protection();
            next_battery_poll_ = now + pdMS_TO_TICKS(i2c_debug::kBatteryPollMs);
        } else if (charger_controller_ && battery_protection_charging_paused_ &&
                   !battery_protection_enabled_.load()) {
            apply_battery_protection();
        }

        // Keep clock update at 1Hz while queue is processed at a higher rate.
        if ((int32_t)(now - next_time_update) >= 0) {
            update_time();
            next_time_update += update_interval;
        }

        check_alarm();
        check_hibernation();
        check_idle_standby();
        check_auto_cathode();

        vTaskDelay(poll_interval);
    }
}

void SystemController::process_message(const SystemMessage &msg)
{
    switch (msg.event) {
        case SystemEvent::BUTTON_PRESSED:
            handle_button_press(msg.data.button_id);
            break;
        case SystemEvent::AUTO_RETURN_CLOCK:
            return_to_clock_mode();
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
        case SystemEvent::SETTINGS_UPDATE:
            if (msg.data.apply.cancel_preview) {
                cancel_display_preview();
            } else if (!msg.data.apply.persist && msg.data.apply.preview_only) {
                apply_display_preview(msg.data.apply.preview_profile);
            } else {
                apply_settings(msg.data.apply.settings,
                               msg.data.apply.has_time ? &msg.data.apply.local_time : nullptr);
                system_state_.save_settings();
                stop_display_preview_timer();
                display_preview_active_ = false;
            }
            break;
        case SystemEvent::BATTERY_UPDATE:
            break;
        default:
            break;
    }
}

void SystemController::apply_settings(const ClockSettings &settings, const struct tm *new_time)
{
    system_state_.set_settings(settings);

    if (new_time && !i2c_debug::kDisableDs3231Rtc) {
        // new_time is local wall-clock as entered by the user. Convert to a
        // UTC epoch using the timezone offset, then dual-write: DS3231 stores
        // UTC and the ESP32 system clock is set to the same UTC instant.
        struct tm wall = *new_time;
        wall.tm_isdst = 0;
        time_t local_as_utc = tm_to_utc_epoch(&wall); // treat fields as UTC
        {
            time_t utc_epoch = local_as_utc - (time_t)settings.tz_offset_hours * 3600;

            struct tm utc_tm;
            gmtime_r(&utc_epoch, &utc_tm);
            if (rtc_.set_time(&utc_tm)) {
                struct timeval tv = { .tv_sec = utc_epoch, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                publish_time_status(true);
                rtc_read_failures_ = 0;
                ESP_LOGI(TAG, "Time set (UTC epoch %lld, tz %+d)",
                         (long long)utc_epoch, settings.tz_offset_hours);
            } else {
                ESP_LOGW(TAG, "Failed to write time to RTC");
            }
        }
    }

    if (wifi_config_ui_active_) {
        DisplayMessage dmsg = {};
        dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
        dmsg.data.color.r = settings.backlight_r;
        dmsg.data.color.g = settings.backlight_g;
        dmsg.data.color.b = settings.backlight_b;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
        dmsg.data.brightness = settings.backlight_brightness;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_EFFECT;
        dmsg.data.effect_id = settings.backlight_effect;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
        dmsg.data.brightness =
            settings.profiles[settings.active_profile_index % kBacklightProfileCount].nixie_brightness;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_NIXIE_TRANSITION;
        dmsg.data.transition_id =
            settings.profiles[settings.active_profile_index % kBacklightProfileCount].nixie_transition;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    } else if (hibernate_state_ == HibernateState::Hibernating) {
        // Keep the display off while hibernation is active.
    } else if (hibernate_state_ == HibernateState::Peek) {
        apply_hibernate_peek_to_display(settings);
    } else {
        DisplayMessage dmsg = {};
        dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
        dmsg.data.color.r = settings.backlight_r;
        dmsg.data.color.g = settings.backlight_g;
        dmsg.data.color.b = settings.backlight_b;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
        dmsg.data.brightness = settings.backlight_brightness;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_EFFECT;
        dmsg.data.effect_id = settings.backlight_effect;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
        dmsg.data.brightness =
            settings.profiles[settings.active_profile_index % kBacklightProfileCount].nixie_brightness;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

        dmsg.command = DisplayCmd::SET_NIXIE_TRANSITION;
        dmsg.data.transition_id =
            settings.profiles[settings.active_profile_index % kBacklightProfileCount].nixie_transition;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    }

    AudioMessage amsg = {};
    amsg.command = AudioCmd::SET_VOLUME;
    amsg.param.volume = settings.volume;
    xQueueSend(audio_daemon_.get_queue(), &amsg, 0);

    if (!i2c_debug::kDisableDs3231Rtc) {
        if (settings.alarm_enabled) {
            // User alarm time is local wall-clock; RTC runs UTC.
            struct tm alarm_local = {};
            alarm_local.tm_hour = settings.alarm_hour;
            alarm_local.tm_min = settings.alarm_minute;
            alarm_local.tm_sec = settings.alarm_second;
            alarm_local.tm_isdst = 0;
            time_t local_as_utc = tm_to_utc_epoch(&alarm_local);
            time_t utc_epoch = local_as_utc - (time_t)settings.tz_offset_hours * 3600;
            struct tm alarm_utc;
            gmtime_r(&utc_epoch, &alarm_utc);
            rtc_.set_alarm1(&alarm_utc);
            rtc_.clear_alarm1_flag();
            rtc_.enable_alarm1_interrupt(true);
            ESP_LOGI(TAG, "Alarm set: local %02u:%02u:%02u (UTC %02u:%02u:%02u, tz %+d)",
                     settings.alarm_hour, settings.alarm_minute, settings.alarm_second,
                     alarm_utc.tm_hour, alarm_utc.tm_min, alarm_utc.tm_sec,
                     settings.tz_offset_hours);
        } else {
            rtc_.enable_alarm1_interrupt(false);
            rtc_.clear_alarm1_flag();
        }
    }

    time_t now_utc = 0;
    time(&now_utc);
    struct tm local_tm;
    time_t local = now_utc + (time_t)settings.tz_offset_hours * 3600;
    gmtime_r(&local, &local_tm);
    evaluate_hibernate_schedule(static_cast<uint8_t>(local_tm.tm_hour),
                        static_cast<uint8_t>(local_tm.tm_min));
}

void SystemController::alarm_stop_timer_cb(void *arg)
{
    auto *self = static_cast<SystemController *>(arg);
    self->stop_alarm_audio();
}

void SystemController::display_preview_timer_cb(void *arg)
{
    auto *self = static_cast<SystemController *>(arg);
    SettingsUpdate update = {};
    update.persist = true;
    update.cancel_preview = true;
    update.preview_only = false;
    self->request_settings_update(update);
}

void SystemController::cancel_alarm_timer()
{
    if (alarm_stop_timer_) {
        esp_timer_stop(alarm_stop_timer_);
    }
}

void SystemController::start_alarm_timer()
{
    if (!alarm_stop_timer_) {
        return;
    }
    cancel_alarm_timer();
    esp_timer_start_once(alarm_stop_timer_, kAlarmMaxDurationMs * 1000ULL);
}

void SystemController::stop_display_preview_timer()
{
    if (display_preview_timer_) {
        esp_timer_stop(display_preview_timer_);
    }
}

void SystemController::start_display_preview_timer()
{
    if (!display_preview_timer_) {
        return;
    }
    stop_display_preview_timer();
    esp_timer_start_once(display_preview_timer_, kDisplayPreviewDurationMs * 1000ULL);
}

void SystemController::stop_alarm_audio()
{
    cancel_alarm_timer();
    if (!alarm_audio_active_) {
        return;
    }
    AudioMessage amsg = {};
    amsg.command = AudioCmd::STOP;
    xQueueSend(audio_daemon_.get_queue(), &amsg, 0);
    alarm_audio_active_ = false;
    ESP_LOGI(TAG, "Alarm audio stopped");
}

void SystemController::check_alarm()
{
    if (i2c_debug::kDisableDs3231Rtc) {
        return;
    }

    ClockSettings settings;
    system_state_.get_settings(&settings);
    if (!settings.alarm_enabled) {
        return;
    }

    bool triggered = false;
    if (!rtc_.alarm1_triggered(&triggered) || !triggered) {
        return;
    }

    // TODO: check the INT pin of DS3231
    rtc_.clear_alarm1_flag();

    uint16_t track = settings.alarm_track;
    if (track == 0) {
        track = 1;
    }

    AudioMessage vol_msg = {};
    vol_msg.command = AudioCmd::SET_VOLUME;
    vol_msg.param.volume = settings.volume;
    xQueueSend(audio_daemon_.get_queue(), &vol_msg, 0);

    AudioMessage amsg = {};
    amsg.command = AudioCmd::PLAY_TRACK_LOOP;
    amsg.param.track_number = track;
    xQueueSend(audio_daemon_.get_queue(), &amsg, 0);
    alarm_audio_active_ = true;
    start_alarm_timer();
    ESP_LOGI(TAG, "Alarm triggered, playing track %u (loop, max %u s)", track,
             kAlarmMaxDurationMs / 1000U);
}

void SystemController::push_current_time_to_display(const struct tm &local_tm)
{
    DisplayMessage msg = {};
    msg.command = DisplayCmd::UPDATE_TIME;
    msg.data.time.yy = static_cast<uint8_t>((local_tm.tm_year + 1900) % 100);
    msg.data.time.mm = static_cast<uint8_t>(local_tm.tm_mon + 1);
    msg.data.time.dd = static_cast<uint8_t>(local_tm.tm_mday);
    msg.data.time.h = static_cast<uint8_t>(local_tm.tm_hour);
    msg.data.time.m = static_cast<uint8_t>(local_tm.tm_min);
    msg.data.time.s = static_cast<uint8_t>(local_tm.tm_sec);
    xQueueSend(display_daemon_.get_queue(), &msg, 0);
}

void SystemController::push_local_time_now()
{
    ClockSettings settings;
    system_state_.get_settings(&settings);

    time_t now_utc = 0;
    time(&now_utc);
    const time_t local = now_utc + static_cast<time_t>(settings.tz_offset_hours) * 3600;

    struct tm local_tm;
    gmtime_r(&local, &local_tm);
    push_current_time_to_display(local_tm);
}

void SystemController::enter_hibernation_mode()
{
    if (wifi_config_ui_active_) {
        return;
    }

    standby_active_ = false;

    current_display_mode_ = DisplayMode::OFF;
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = DisplayMode::OFF;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = 0;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_EFFECT;
    dmsg.data.effect_id = 3;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    ESP_LOGI(TAG, "Entering hibernation mode,turn off LED baclkight and tube");
}

void SystemController::peek_from_hibernate()
{
    ClockSettings settings;
    system_state_.get_settings(&settings);
    apply_hibernate_peek_to_display(settings);

    current_display_mode_ = DisplayMode::CLOCK_HHMMSS;
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = DisplayMode::CLOCK_HHMMSS;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    push_local_time_now();

    hibernate_state_ = HibernateState::Peek;
    hibernation_peek_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kHibernationPeekMs);
    ESP_LOGI(TAG, "Hibernate: display awake for %u s", kHibernationPeekMs / 1000U);
}

void SystemController::restore_user_profile()
{
    ClockSettings settings;
    system_state_.get_settings(&settings);
    const BacklightProfile &profile =
        settings.profiles[settings.active_profile_index % kBacklightProfileCount];
    apply_profile_to_display(profile);
}

void SystemController::evaluate_hibernate_schedule(uint8_t hour, uint8_t minute)
{
    if (wifi_config_ui_active_) {
        return;
    }

    ClockSettings settings;
    system_state_.get_settings(&settings);
    const bool in_window = hibernate_is_active(settings, hour, minute);

    if (!in_window) {
        if (hibernate_state_ != HibernateState::Normal || hibernate_window_active_) {
            restore_user_profile();
            if (hibernate_state_ != HibernateState::Normal) {
                current_display_mode_ = DisplayMode::CLOCK_HHMMSS;
                DisplayMessage dmsg = {};
                dmsg.command = DisplayCmd::SET_MODE;
                dmsg.data.mode = DisplayMode::CLOCK_HHMMSS;
                xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            }
            hibernate_state_ = HibernateState::Normal;
            hibernate_window_active_ = false;
            note_user_activity();
            ESP_LOGI(TAG, "Hibernate: normal operation");
        }
        return;
    }

    hibernate_window_active_ = true;

    if (hibernate_state_ == HibernateState::Peek) {
        return;
    }

    if (standby_active_) {
        standby_active_ = false;
    }

    if (hibernate_state_ != HibernateState::Hibernating) {
        hibernate_state_ = HibernateState::Hibernating;
        enter_hibernation_mode();
    }
}

void SystemController::check_hibernation()
{
    if (wifi_config_ui_active_) {
        return;
    }

    if (hibernate_state_ != HibernateState::Peek) {
        return;
    }
    if ((int32_t)(xTaskGetTickCount() - hibernation_peek_deadline_) < 0) {
        return;
    }
    hibernate_state_ = HibernateState::Hibernating;
    enter_hibernation_mode();
    ESP_LOGI(TAG, "Hibernate: idle timeout, display asleep");
}

uint8_t SystemController::scale_standby_brightness(uint8_t value)
{
    return static_cast<uint8_t>(value * standby_brightness_factor + 0.5f);
}

void SystemController::note_user_activity()
{
    idle_standby_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kIdleStandbyMs);
}

void SystemController::enter_standby()
{
    ClockSettings settings;
    system_state_.get_settings(&settings);
    const BacklightProfile &profile =
        settings.profiles[settings.active_profile_index % kBacklightProfileCount];

    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
    dmsg.data.brightness = scale_standby_brightness(profile.backlight_brightness);
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = scale_standby_brightness(profile.nixie_brightness);
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    standby_active_ = true;
    ESP_LOGI(TAG, "Standby: dimmed to %u%% of profile",
             static_cast<unsigned>(standby_brightness_factor * 100.0f + 0.5f));
}

void SystemController::exit_standby()
{
    if (!standby_active_) {
        return;
    }

    ClockSettings settings;
    system_state_.get_settings(&settings);
    const BacklightProfile &profile =
        settings.profiles[settings.active_profile_index % kBacklightProfileCount];
    apply_profile_to_display(profile);
    standby_active_ = false;
    ESP_LOGI(TAG, "Standby: restored profile brightness");
}

void SystemController::check_idle_standby()
{
    if (hibernate_state_ != HibernateState::Normal || standby_active_) {
        return;
    }
    if (current_display_mode_ != DisplayMode::CLOCK_HHMMSS) {
        return;
    }
    if ((int32_t)(xTaskGetTickCount() - idle_standby_deadline_) < 0) {
        return;
    }
    enter_standby();
}

void SystemController::start_auto_cathode()
{
    if (standby_active_) {
        exit_standby();
    }

    current_display_mode_ = DisplayMode::CATHODE_POISONING;
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = DisplayMode::CATHODE_POISONING;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    next_auto_cathode_ = xTaskGetTickCount() + pdMS_TO_TICKS(kAutoCathodeIntervalMs);
    ESP_LOGI(TAG, "Auto cathode poisoning started");
}

void SystemController::check_auto_cathode()
{
    if (hibernate_state_ != HibernateState::Normal) {
        return;
    }
    if ((int32_t)(xTaskGetTickCount() - next_auto_cathode_) < 0) {
        return;
    }

    if (is_alarm_audio_active() ||
        current_display_mode_ == DisplayMode::POMODORO ||
        current_display_mode_ == DisplayMode::DIVERGENCE_METER ||
        current_display_mode_ == DisplayMode::CATHODE_POISONING ||
        current_display_mode_ == DisplayMode::OFF) {
        return;
    }

    start_auto_cathode();
}

void SystemController::update_time()
{
    TimeStatus time_status;
    system_state_.get_time(&time_status);
    if (!time_status.valid) {
        return;
    }

    time_t now_utc;
    time(&now_utc);
    publish_time_status(true);

    ClockSettings settings;
    system_state_.get_settings(&settings);

    // Drive the display from ESP32 system time (UTC) converted to local using
    // the timezone offset. This avoids hitting the shared I2C bus every second.
    time_t local = now_utc + (time_t)settings.tz_offset_hours * 3600;

    struct tm local_tm;
    gmtime_r(&local, &local_tm);

    DisplayMessage msg;
    msg.command = DisplayCmd::UPDATE_TIME;
    msg.data.time.yy = static_cast<uint8_t>((local_tm.tm_year + 1900) % 100);
    msg.data.time.mm = static_cast<uint8_t>(local_tm.tm_mon + 1);
    msg.data.time.dd = static_cast<uint8_t>(local_tm.tm_mday);
    msg.data.time.h = static_cast<uint8_t>(local_tm.tm_hour);
    msg.data.time.m = static_cast<uint8_t>(local_tm.tm_min);
    msg.data.time.s = static_cast<uint8_t>(local_tm.tm_sec);
    xQueueSend(display_daemon_.get_queue(), &msg, 0);

    evaluate_hibernate_schedule(static_cast<uint8_t>(local_tm.tm_hour),
                        static_cast<uint8_t>(local_tm.tm_min));
}

void SystemController::sync_time_from_rtc()
{
    if (i2c_debug::kDisableDs3231Rtc) {
        return;
    }

    // If the oscillator stopped, the stored time is garbage; wait for the user
    // to set the time before trusting/displaying it.
    bool osf = false;
    if (rtc_.oscillator_stopped(&osf) && osf) {
        // Running without a VBAT battery: seed a default time so OSF is cleared
        // (set_time() clears it) and the clock becomes usable instead of
        // warning on every resync.
        if (i2c_debug::kSeedRtcOnOscStop) {
            struct tm seed_tm;
            build_time_to_tm(&seed_tm);
            if (rtc_.set_time(&seed_tm)) { // also clears OSF
                time_t utc_epoch = tm_to_utc_epoch(&seed_tm);
                struct timeval tv = { .tv_sec = utc_epoch, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                publish_time_status(true);
                rtc_read_failures_ = 0;
                ESP_LOGW(TAG, "RTC OSF set; seeded build time (UTC epoch %lld) and cleared OSF",
                         (long long)utc_epoch);
                return;
            }
            ESP_LOGW(TAG, "RTC OSF set; failed to seed default time");
        }
        publish_time_status(false);
        ESP_LOGW(TAG, "RTC oscillator stop flag set; awaiting time set");
        return;
    }

    struct tm utc_tm;
    if (rtc_.get_time(&utc_tm)) {
        rtc_read_failures_ = 0;
        utc_tm.tm_isdst = 0;
        time_t utc_epoch = tm_to_utc_epoch(&utc_tm);
        struct timeval tv = { .tv_sec = utc_epoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        publish_time_status(true);
        ESP_LOGI(TAG, "Synced system time from RTC (UTC epoch %lld)",
                 (long long)utc_epoch);
    } else {
        if (rtc_read_failures_ < kMaxReadFailures) {
            rtc_read_failures_++;
        }
        ESP_LOGW(TAG, "Failed to read time from RTC (%u/%u)",
                 rtc_read_failures_, kMaxReadFailures);
        if (rtc_read_failures_ >= kMaxReadFailures) {
            publish_time_status(false);
        }
    }
}

void SystemController::publish_time_status(bool valid)
{
    time_t now_utc = 0;
    if (valid) {
        time(&now_utc);
    }
    system_state_.update_time(now_utc, valid);
}

void SystemController::invalidate_battery_status()
{
    BatteryStatus status = {};
    status.valid = false;
    status.updated_at = 0;
    system_state_.update_battery(status);
}

void SystemController::sync_battery_from_gauge()
{
    if (!gasgauge_service_ || i2c_debug::kDisableGasgauge) {
        return;
    }

    if (!gasgauge_ready_) {
        GasgaugeDeviceInfo info;
        if (!gasgauge_service_->probe_device_info(info) || !gasgauge_service_->is_ready()) {
            invalidate_battery_status();
            return;
        }
        gasgauge_ready_ = true;
        battery_read_failures_ = 0;
        ESP_LOGI(TAG, "Gasgauge ready");
    }

    GasgaugeData data;
    if (!gasgauge_service_->read_data(data)) {
        battery_read_failures_++;
        if (battery_read_failures_ >= kMaxBatteryReadFailures) {
            ESP_LOGW(TAG, "Gasgauge read failed %u times; will retry probe",
                     battery_read_failures_);
            gasgauge_ready_ = false;
            battery_read_failures_ = 0;
            invalidate_battery_status();
        }
        return;
    }

    battery_read_failures_ = 0;
    BatteryStatus status = {
        .soc = data.soc,
        .soh = data.soh,
        .battery_voltage_mv = data.voltage_mv,
        .battery_current_ma = data.current_ma,
        .valid = true,
        .updated_at = xTaskGetTickCount(),
    };
    system_state_.update_battery(status);
    ESP_LOGD(TAG, "Battery: %u%%, %u mV, %d mA, SOH %u%%",
             data.soc, data.voltage_mv, data.current_ma, data.soh);
}

void SystemController::apply_battery_protection()
{
    if (!charger_controller_) {
        return;
    }

    if (!battery_protection_enabled_.load()) {
        if (battery_protection_charging_paused_) {
            if (charger_controller_->enable_charging()) {
                battery_protection_charging_paused_ = false;
                ESP_LOGI(TAG, "Battery protection disabled; charging resumed");
            } else {
                ESP_LOGW(TAG, "Battery protection disabled but failed to resume charging");
            }
        }
        return;
    }

    BatteryStatus battery;
    if (!system_state_.get_battery(&battery) || !battery.valid) {
        return;
    }

    if (battery.soc >= kBatteryProtectionSocLimit) {
        if (!battery_protection_charging_paused_) {
            if (charger_controller_->disable_charging()) {
                battery_protection_charging_paused_ = true;
                ESP_LOGI(TAG, "Battery protection: charging stopped at %u%% (limit %u%%)",
                         battery.soc, kBatteryProtectionSocLimit);
            } else {
                ESP_LOGW(TAG, "Battery protection: failed to stop charging at %u%%", battery.soc);
            }
        }
        return;
    }

    if (battery.soc <= kBatteryProtectionSocResume && battery_protection_charging_paused_) {
        if (charger_controller_->enable_charging()) {
            battery_protection_charging_paused_ = false;
            ESP_LOGI(TAG, "Battery protection: charging resumed at %u%% (resume %u%%)",
                     battery.soc, kBatteryProtectionSocResume);
        } else {
            ESP_LOGW(TAG, "Battery protection: failed to resume charging at %u%%", battery.soc);
        }
    }
}

void SystemController::set_battery_protection_enabled(bool enabled)
{
    battery_protection_enabled_.store(enabled);
    ESP_LOGI(TAG, "Battery protection %s", enabled ? "enabled" : "disabled");
}

bool SystemController::is_battery_protection_enabled() const
{
    return battery_protection_enabled_.load();
}

void SystemController::request_settings_update(const ClockSettings &settings,
                                               const struct tm *local_time)
{
    SettingsUpdate update = {};
    update.settings = settings;
    update.has_time = (local_time != nullptr);
    update.persist = true;
    update.cancel_preview = false;
    update.preview_only = false;
    if (local_time) {
        update.local_time = *local_time;
    }
    request_settings_update(update);
}

void SystemController::request_settings_update(const SettingsUpdate &update)
{
    SystemMessage msg = {};
    msg.event = SystemEvent::SETTINGS_UPDATE;
    msg.data.apply = update;
    xQueueSend(queue_, &msg, 0);
}

bool SystemController::get_time_status(struct tm *local_out, bool *time_valid,
                                       bool *osf, float *temperature,
                                       time_t *unix_utc)
{
    TimeStatus time_status;
    system_state_.get_time(&time_status);
    if (time_valid) {
        *time_valid = time_status.valid;
    }
    time_t now_utc = time_status.valid ? time_status.unix_utc : 0;
    if (time_status.valid) {
        time(&now_utc);
    }
    if (unix_utc) {
        *unix_utc = now_utc;
    }
    if (local_out) {
        ClockSettings settings;
        system_state_.get_settings(&settings);
        time_t local = now_utc + (time_t)settings.tz_offset_hours * 3600;
        gmtime_r(&local, local_out);
    }
    if (osf) {
        bool stopped = false;
        if (!i2c_debug::kDisableDs3231Rtc) {
            rtc_.oscillator_stopped(&stopped);
        }
        *osf = stopped;
    }
    if (temperature) {
        *temperature = 0.0f;
        if (!i2c_debug::kDisableDs3231Rtc) {
            rtc_.get_temperature(temperature);
        }
    }
    return true;
}

DisplayMode SystemController::next_display_mode(DisplayMode mode)
{
    switch (mode) {
        case DisplayMode::CLOCK_HHMMSS:
            return DisplayMode::DATE_YYMMDD;
        case DisplayMode::DATE_YYMMDD:
            return DisplayMode::POMODORO;
        case DisplayMode::POMODORO:
            return DisplayMode::DIVERGENCE_METER;
        case DisplayMode::DIVERGENCE_METER:
            return DisplayMode::CATHODE_POISONING;
        case DisplayMode::CATHODE_POISONING:
        default:
            return DisplayMode::CLOCK_HHMMSS;
    }
}

bool SystemController::is_alarm_audio_active() const
{
    return alarm_audio_active_;
}

void SystemController::apply_profile_to_display(const BacklightProfile &profile)
{
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
    dmsg.data.color.r = profile.r;
    dmsg.data.color.g = profile.g;
    dmsg.data.color.b = profile.b;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
    dmsg.data.brightness = profile.backlight_brightness;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_EFFECT;
    dmsg.data.effect_id = profile.backlight_effect;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = profile.nixie_brightness;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_TRANSITION;
    dmsg.data.transition_id = profile.nixie_transition;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    standby_active_ = false;
    note_user_activity();
}

void SystemController::apply_display_preview(const BacklightProfile &profile)
{
    display_preview_active_ = true;
    display_preview_ = profile;
    start_display_preview_timer();
    if (hibernate_state_ == HibernateState::Hibernating) {
        return;
    }
    if (hibernate_state_ == HibernateState::Peek) {
        return;
    }
    apply_profile_to_display(profile);
}

void SystemController::cancel_display_preview()
{
    stop_display_preview_timer();
    if (!display_preview_active_) {
        return;
    }
    display_preview_active_ = false;
    if (hibernate_state_ == HibernateState::Hibernating) {
        return;
    }
    restore_user_profile();
}

void SystemController::apply_hibernate_peek_to_display(const ClockSettings &settings)
{
    const BacklightProfile &profile =
        settings.profiles[settings.active_profile_index % kBacklightProfileCount];

    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_EFFECT;
    dmsg.data.effect_id = 3;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = kHibernatePeekNixieBrightness;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::SET_NIXIE_TRANSITION;
    dmsg.data.transition_id = profile.nixie_transition;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
}

void SystemController::return_to_clock_mode()
{
    current_display_mode_ = DisplayMode::CLOCK_HHMMSS;
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = DisplayMode::CLOCK_HHMMSS;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    push_local_time_now();
    note_user_activity();
    ESP_LOGI(TAG, "Auto-return to clock mode");
}

void SystemController::cycle_display_mode()
{
    const DisplayMode prev = current_display_mode_;
    current_display_mode_ = next_display_mode(current_display_mode_);
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = current_display_mode_;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    if (prev == DisplayMode::POMODORO) {
        ClockSettings settings;
        system_state_.get_settings(&settings);
        apply_profile_to_display(settings.profiles[settings.active_profile_index]);
    }
    const bool is_clockish = current_display_mode_ == DisplayMode::CLOCK_HHMMSS ||
                             current_display_mode_ == DisplayMode::DATE_YYMMDD;
    if (is_clockish) {
        push_local_time_now();
    }
    if (current_display_mode_ == DisplayMode::CLOCK_HHMMSS) {
        note_user_activity();
    }
    ESP_LOGI(TAG, "Display mode cycled to %u", static_cast<unsigned>(current_display_mode_));
}

void SystemController::cycle_profile()
{
    if (display_preview_active_) {
        cancel_display_preview();
    }
    ClockSettings settings;
    system_state_.get_settings(&settings);
    settings.active_profile_index =
        static_cast<uint8_t>((settings.active_profile_index + 1) % kBacklightProfileCount);
    system_state_.set_settings(settings);

    const BacklightProfile &profile = settings.profiles[settings.active_profile_index];
    settings.backlight_r = profile.r;
    settings.backlight_g = profile.g;
    settings.backlight_b = profile.b;
    settings.backlight_brightness = profile.backlight_brightness;
    settings.backlight_effect = profile.backlight_effect;
    system_state_.set_settings(settings);
    system_state_.save_settings();

    apply_profile_to_display(profile);
    ESP_LOGI(TAG, "Active profile: %u", settings.active_profile_index);
}

void SystemController::handle_button_press(uint8_t button_id)
{
    ESP_LOGI(TAG, "Button pressed: %u", button_id);

    if (hibernate_state_ == HibernateState::Hibernating && !alarm_audio_active_) {
        peek_from_hibernate();
        return;
    }

    if (hibernate_state_ == HibernateState::Peek) {
        hibernation_peek_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kHibernationPeekMs);
        return;
    }

    if (standby_active_) {
        exit_standby();
        note_user_activity();
        return;
    }

    note_user_activity();

    if (wifi_config_ui_active_) {
        return;
    }

    if (button_id == kButtonAlarmStop) {
        if (is_alarm_audio_active()) {
            stop_alarm_audio();
            return;
        }

        if (current_display_mode_ == DisplayMode::POMODORO) {
            DisplayMessage dmsg = {};
            dmsg.command = DisplayCmd::POMODORO_START;
            xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            ESP_LOGI(TAG, "Pomodoro started");
            return;
        }

        if (current_display_mode_ == DisplayMode::DIVERGENCE_METER) {
            DisplayMessage dmsg = {};
            dmsg.command = DisplayCmd::DIVERGENCE_RESTART;
            xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            ESP_LOGI(TAG, "Divergence meter restarted");
        }
        return;
    }

    if (button_id == kButtonModeCycle) {
        cycle_display_mode();
        return;
    }

    if (button_id == kButtonProfileCycle) {
        if (current_display_mode_ == DisplayMode::POMODORO) {
            return;
        }
        cycle_profile();
    }
}

bool SystemController::apply_ntp_utc(time_t ntp_utc, int *drift_sec_out)
{
    if (i2c_debug::kDisableDs3231Rtc) {
        return false;
    }

    static constexpr int kMaxVerifyDriftSec = 60;
    static constexpr int kMaxAttempts = 3;
    static constexpr TickType_t kRtcSettleDelay = pdMS_TO_TICKS(50);

    int last_drift = 0;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
        struct tm utc_tm = {};
        gmtime_r(&ntp_utc, &utc_tm);
        if (!rtc_.set_time(&utc_tm)) {
            ESP_LOGW(TAG, "NTP apply attempt %d/%d: RTC write failed", attempt, kMaxAttempts);
            vTaskDelay(kRtcSettleDelay);
            continue;
        }

        struct timeval tv = {.tv_sec = ntp_utc, .tv_usec = 0};
        settimeofday(&tv, nullptr);
        vTaskDelay(kRtcSettleDelay);

        struct tm rtc_tm = {};
        if (!rtc_.get_time(&rtc_tm)) {
            ESP_LOGW(TAG, "NTP apply attempt %d/%d: RTC readback failed", attempt, kMaxAttempts);
            continue;
        }
        rtc_tm.tm_isdst = 0;
        const time_t rtc_utc = tm_to_utc_epoch(&rtc_tm);
        last_drift =
            static_cast<int>(std::llabs(static_cast<long long>(ntp_utc - rtc_utc)));

        if (last_drift < kMaxVerifyDriftSec) {
            if (drift_sec_out) {
                *drift_sec_out = last_drift;
            }
            publish_time_status(true);
            rtc_read_failures_ = 0;

            ClockSettings settings{};
            system_state_.get_settings(&settings);
            settings.rtc_calibrated = true;
            system_state_.set_settings(settings);
            system_state_.save_settings();

            ESP_LOGI(TAG, "NTP applied UTC %lld verified drift=%d s (attempt %d)",
                     static_cast<long long>(ntp_utc), last_drift, attempt);
            return true;
        }

        ESP_LOGW(TAG, "NTP verify drift=%d s >= %d s (attempt %d/%d)", last_drift,
                 kMaxVerifyDriftSec, attempt, kMaxAttempts);
    }

    if (drift_sec_out) {
        *drift_sec_out = last_drift;
    }
    ESP_LOGE(TAG, "NTP apply failed after %d attempts (last drift=%d s)", kMaxAttempts,
             last_drift);
    return false;
}

void SystemController::enter_wifi_config_ui(uint16_t session_code)
{
    const bool first_entry = !wifi_config_ui_active_;
    if (first_entry) {
        wifi_config_ui_active_ = true;
        display_mode_before_wifi_config_ = current_display_mode_;
        hibernate_state_before_wifi_config_ = hibernate_state_;
        if (hibernate_state_ == HibernateState::Hibernating ||
            hibernate_state_ == HibernateState::Peek) {
            hibernate_state_ = HibernateState::Normal;
        }
    }

    wifi_config_phase_ = WifiConfigPhase::WaitingClient;

    ClockSettings settings{};
    system_state_.get_settings(&settings);
    const BacklightProfile &profile =
        settings.profiles[settings.active_profile_index % kBacklightProfileCount];
    uint8_t nixie_brightness = profile.nixie_brightness;
    if (nixie_brightness == 0) {
        nixie_brightness = kWifiConfigNixieBrightness;
    }

    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = nixie_brightness;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    dmsg.command = DisplayCmd::ENTER_WIFI_CONFIG;
    dmsg.data.number = session_code;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    current_display_mode_ = DisplayMode::CONFIG_CODE;
    ESP_LOGI(TAG, "WiFi config UI: session code %04u%s", session_code,
             first_entry ? "" : " (refresh)");
}

void SystemController::on_wifi_config_client_connected()
{
    if (!wifi_config_ui_active_) {
        return;
    }

    wifi_config_phase_ = WifiConfigPhase::ClientConnected;

    ClockSettings settings{};
    system_state_.get_settings(&settings);
    apply_settings(settings, nullptr);
    return_to_clock_mode();
    ESP_LOGI(TAG, "WiFi config: client connected, showing clock with profile backlight");
}

void SystemController::exit_wifi_config_ui()
{
    if (!wifi_config_ui_active_) {
        return;
    }
    wifi_config_ui_active_ = false;
    wifi_config_phase_ = WifiConfigPhase::WaitingClient;

    const HibernateState saved_hibernate = hibernate_state_before_wifi_config_;
    hibernate_state_before_wifi_config_ = HibernateState::Normal;

    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::EXIT_WIFI_CONFIG;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

    ClockSettings settings{};
    system_state_.get_settings(&settings);
    time_t now_utc = 0;
    time(&now_utc);
    const time_t local = now_utc + static_cast<time_t>(settings.tz_offset_hours) * 3600;
    struct tm local_tm = {};
    gmtime_r(&local, &local_tm);
    const bool in_hibernate_window =
        hibernate_is_active(settings, static_cast<uint8_t>(local_tm.tm_hour),
                            static_cast<uint8_t>(local_tm.tm_min));

    if (saved_hibernate == HibernateState::Hibernating && in_hibernate_window) {
        hibernate_state_ = HibernateState::Hibernating;
        hibernate_window_active_ = true;
        enter_hibernation_mode();
    } else if (display_mode_before_wifi_config_ == DisplayMode::CLOCK_HHMMSS ||
               display_mode_before_wifi_config_ == DisplayMode::DATE_YYMMDD) {
        return_to_clock_mode();
    } else {
        current_display_mode_ = display_mode_before_wifi_config_;
        dmsg.command = DisplayCmd::SET_MODE;
        dmsg.data.mode = display_mode_before_wifi_config_;
        xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    }
    ESP_LOGI(TAG, "WiFi config UI exited");
}
