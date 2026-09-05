#pragma once

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/uart.h"
#include "message_types.h"
#include "display_board_config.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "ds3231/ds3231.h"
#include "system_state.h"

class GasgaugeService;
class ChargerController;

struct HardwareHandles {
    i2c_port_t i2c_port;
    rmt_channel_handle_t led_rmt_channel;
    rmt_encoder_handle_t led_rmt_encoder;
    uart_port_t audio_uart_port;
    DisplayBoardType display_board_type;
};

class SystemController
{
public:
    static HardwareHandles init_hardware();

    SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon,
                     SystemState &system_state,
                     GasgaugeService *gasgauge_service = nullptr,
                     bool gasgauge_ready_at_boot = false,
                     ChargerController *charger_controller = nullptr);
    ~SystemController();

    void start();
    QueueHandle_t get_queue() const;

    // Direct apply (used only at boot, before the task is running).
    void apply_settings(const ClockSettings &settings, const struct tm *new_time);

    // Thread-safe entry point for other tasks (web/CLI): enqueues the change
    // so the SystemController task remains the sole owner of rtc_/settings.
    void request_settings_update(const ClockSettings &settings, const struct tm *local_time);
    void request_settings_update(const SettingsUpdate &update);

    // Snapshot of current time state for status endpoints (thread-safe read of
    // system time + a cached RTC read).
    bool get_time_status(struct tm *local_out, bool *time_valid, bool *osf, float *temperature,
                         time_t *unix_utc = nullptr);

    bool apply_ntp_utc(time_t ntp_utc, int *drift_sec_out = nullptr);

    void enter_wifi_config_ui(uint16_t session_code);
    void on_wifi_config_client_connected();
    void exit_wifi_config_ui();

    // Battery protection caps charge at 80% SOC to extend cell life.
    // Reserved for Web UI wiring (refactor/webui); defaults to disabled.
    void set_battery_protection_enabled(bool enabled);
    bool is_battery_protection_enabled() const;

private:
    enum class HibernateState : uint8_t
    {
        Normal,
        Peek,
        Hibernating
    };

    enum class WifiConfigPhase : uint8_t
    {
        WaitingClient,
        ClientConnected,
    };

    static void task_entry(void *param);
    static void alarm_stop_timer_cb(void *arg);
    static void display_preview_timer_cb(void *arg);
    void loop();
    void process_message(const SystemMessage &msg);
    void update_time();
    void sync_time_from_rtc();
    void publish_time_status(bool valid);
    void sync_battery_from_gauge();
    void apply_battery_protection();
    void invalidate_battery_status();
    void check_alarm();
    void check_hibernation();
    void check_idle_standby();
    void check_auto_cathode();
    void evaluate_hibernate_schedule(uint8_t hour, uint8_t minute);
    void enter_hibernation_mode();
    void peek_from_hibernate();
    void restore_user_profile();
    void note_user_activity();
    void enter_standby();
    void exit_standby();
    void start_auto_cathode();
    void push_current_time_to_display(const struct tm &local_tm);
    void push_local_time_now();
    void cancel_alarm_timer();
    void start_alarm_timer();
    void stop_alarm_audio();
    void start_display_preview_timer();
    void stop_display_preview_timer();
    void handle_button_press(uint8_t button_id);
    void return_to_clock_mode();
    void cycle_display_mode();
    void cycle_profile();
    void apply_profile_to_display(const BacklightProfile &profile);
    void apply_display_preview(const BacklightProfile &profile);
    void cancel_display_preview();
    void apply_hibernate_peek_to_display(const ClockSettings &settings);
    bool is_alarm_audio_active() const;
    static DisplayMode next_display_mode(DisplayMode mode);
    static uint8_t scale_standby_brightness(uint8_t value);

    DisplayDaemon &display_daemon_;
    AudioDaemon &audio_daemon_;
    SystemState &system_state_;
    GasgaugeService *gasgauge_service_;
    ChargerController *charger_controller_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;

    Ds3231 rtc_;
    uint8_t rtc_read_failures_;
    uint8_t battery_read_failures_;
    bool gasgauge_ready_;
    bool alarm_audio_active_;
    esp_timer_handle_t alarm_stop_timer_;
    esp_timer_handle_t display_preview_timer_;
    HibernateState hibernate_state_;
    bool hibernate_window_active_;
    TickType_t hibernation_peek_deadline_;
    DisplayMode current_display_mode_;
    TickType_t next_rtc_sync_;
    TickType_t next_battery_poll_;
    bool standby_active_;
    TickType_t idle_standby_deadline_;
    TickType_t next_auto_cathode_;
    bool display_preview_active_;
    BacklightProfile display_preview_;
    bool wifi_config_ui_active_;
    WifiConfigPhase wifi_config_phase_;
    DisplayMode display_mode_before_wifi_config_;
    HibernateState hibernate_state_before_wifi_config_;

    std::atomic<bool> battery_protection_enabled_;
    bool battery_protection_charging_paused_;

    static constexpr uint8_t kMaxBatteryReadFailures = 3;
    static constexpr uint8_t kBatteryProtectionSocLimit = 80;
    static constexpr uint8_t kBatteryProtectionSocResume = 78;
    static constexpr uint8_t kHibernatePeekNixieBrightness = 50;
    static constexpr uint32_t kAlarmMaxDurationMs = 180000;
    static constexpr uint32_t kDisplayPreviewDurationMs = 2000;
    static constexpr uint32_t kHibernationPeekMs = 5000;
    static constexpr uint32_t kIdleStandbyMs = 60000;
    static constexpr float standby_brightness_factor = 0.25f;
    static constexpr uint32_t kAutoCathodeIntervalMs = 15 * 60 * 1000;
    static constexpr uint8_t kWifiConfigNixieBrightness = 200;
};
