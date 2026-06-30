#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/uart.h"
#include "message_types.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "ds3231/ds3231.h"
#include "system_state.h"

class GasgaugeService;

struct HardwareHandles {
    i2c_port_t i2c_port;
    rmt_channel_handle_t led_rmt_channel;
    rmt_encoder_handle_t led_rmt_encoder;
    uart_port_t audio_uart_port;
};

class SystemController
{
public:
    static HardwareHandles init_hardware();

    SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon,
                     SystemState &system_state,
                     GasgaugeService *gasgauge_service = nullptr,
                     bool gasgauge_ready_at_boot = false);
    ~SystemController();

    void start();
    QueueHandle_t get_queue() const;

    // Direct apply (used only at boot, before the task is running).
    void apply_settings(const ClockSettings &settings, const struct tm *new_time);

    // Thread-safe entry point for other tasks (web/CLI): enqueues the change
    // so the SystemController task remains the sole owner of rtc_/settings.
    void request_settings_update(const ClockSettings &settings, const struct tm *local_time);

    // Snapshot of current time state for status endpoints (thread-safe read of
    // system time + a cached RTC read).
    bool get_time_status(struct tm *local_out, bool *time_valid, bool *osf, float *temperature,
                         time_t *unix_utc = nullptr);

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const SystemMessage &msg);
    void update_time();
    void sync_time_from_rtc();
    void publish_time_status(bool valid);
    void sync_battery_from_gauge();
    void invalidate_battery_status();
    void check_alarm();

    DisplayDaemon &display_daemon_;
    AudioDaemon &audio_daemon_;
    SystemState &system_state_;
    GasgaugeService *gasgauge_service_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;

    Ds3231 rtc_;
    uint8_t rtc_read_failures_;
    uint8_t battery_read_failures_;
    bool gasgauge_ready_;
    TickType_t next_rtc_sync_;
    TickType_t next_battery_poll_;

    static constexpr uint8_t kMaxBatteryReadFailures = 3;
};
