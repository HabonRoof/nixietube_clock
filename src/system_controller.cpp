#include "system_controller.h"
#include "button_config.h"
#include "esp_log.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/time.h>
#include "system_state.h"
#include "gasgauge_service.h"
#include "i2c_debug_config.h"
#include "i2c_bus.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *TAG = "SystemController";

// Timekeeping policy: maintain ESP32 system time (UTC) and periodically
// resync from the DS3231 (the ±2ppm truth source) to correct drift.
static constexpr uint32_t kResyncIntervalMs = 30000; // 1 hour
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

// Hardware Configuration
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSda = static_cast<gpio_num_t>(6);
constexpr gpio_num_t kI2cScl = static_cast<gpio_num_t>(5);
constexpr uint32_t kI2cClockHz = 400000;

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
    i2c_bus_init(kI2cPort);
    handles.i2c_port = kI2cPort;
    ESP_LOGI(TAG, "I2C Initialized");

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

    // 4b. Initialize 74HC238 anode mux (blank until scan task starts)
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
                                 bool gasgauge_ready_at_boot)
    : display_daemon_(display_daemon),
      audio_daemon_(audio_daemon),
      system_state_(system_state),
      gasgauge_service_(gasgauge_service),
      queue_(nullptr),
      task_handle_(nullptr),
      rtc_(kI2cPort),
      rtc_read_failures_(0),
      battery_read_failures_(0),
      gasgauge_ready_(gasgauge_ready_at_boot),
      alarm_audio_active_(false),
      current_display_mode_(DisplayMode::CLOCK_HHMMSS),
      next_rtc_sync_(0),
      next_battery_poll_(0)
{
    queue_ = xQueueCreate(32, sizeof(SystemMessage));
    
    if (i2c_debug::kDisableDs3231Rtc) {
        ESP_LOGW(TAG, "DS3231 RTC I2C disabled");
    } else if (rtc_.init()) {
        ESP_LOGI(TAG, "RTC Initialized");
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

    const TickType_t poll_interval = pdMS_TO_TICKS(20);
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update time every second
    TickType_t next_time_update = xTaskGetTickCount() + update_interval;

    // Initial sync: seed ESP32 system time from the DS3231 (UTC).
    sync_time_from_rtc();
    next_rtc_sync_ = xTaskGetTickCount() + pdMS_TO_TICKS(kResyncIntervalMs);
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
            next_battery_poll_ = now + pdMS_TO_TICKS(i2c_debug::kBatteryPollMs);
        }

        // Keep clock update at 1Hz while queue is processed at a higher rate.
        if ((int32_t)(now - next_time_update) >= 0) {
            update_time();
            next_time_update += update_interval;
        }

        check_alarm();

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
            apply_settings(msg.data.apply.settings,
                           msg.data.apply.has_time ? &msg.data.apply.local_time : nullptr);
            system_state_.save_settings();
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
    amsg.command = AudioCmd::PLAY_TRACK;
    amsg.param.track_number = track;
    xQueueSend(audio_daemon_.get_queue(), &amsg, 0);
    alarm_audio_active_ = true;
    ESP_LOGI(TAG, "Alarm triggered, playing track %u", track);
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

void SystemController::request_settings_update(const ClockSettings &settings,
                                               const struct tm *local_time)
{
    SystemMessage msg = {};
    msg.event = SystemEvent::SETTINGS_UPDATE;
    msg.data.apply.settings = settings;
    msg.data.apply.has_time = (local_time != nullptr);
    if (local_time) {
        msg.data.apply.local_time = *local_time;
    }
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
}

void SystemController::return_to_clock_mode()
{
    current_display_mode_ = DisplayMode::CLOCK_HHMMSS;
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = DisplayMode::CLOCK_HHMMSS;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    ESP_LOGI(TAG, "Auto-return to clock mode");
}

void SystemController::cycle_display_mode()
{
    current_display_mode_ = next_display_mode(current_display_mode_);
    DisplayMessage dmsg = {};
    dmsg.command = DisplayCmd::SET_MODE;
    dmsg.data.mode = current_display_mode_;
    xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
    ESP_LOGI(TAG, "Display mode cycled to %u", static_cast<unsigned>(current_display_mode_));
}

void SystemController::cycle_profile()
{
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

    if (button_id == kButtonAlarmStop) {
        if (is_alarm_audio_active()) {
            AudioMessage amsg = {};
            amsg.command = AudioCmd::STOP;
            xQueueSend(audio_daemon_.get_queue(), &amsg, 0);
            alarm_audio_active_ = false;
            ESP_LOGI(TAG, "Alarm audio stopped");
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
        cycle_profile();
    }
}
