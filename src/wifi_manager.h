#pragma once

#include <cstdint>
#include <ctime>
#include <functional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "wifi_credentials.h"

enum class WifiManagerState : uint8_t
{
    Off,
    ConfigAp,
    StaNtpSync,
};

struct WifiApStatus
{
    bool active;
    uint16_t session_code;
    char ssid[32];
    char password[32];
    int remaining_sec;
    uint8_t client_count;
};

class WifiManager
{
public:
    using ConfigEnterFn = std::function<void(uint16_t session_code)>;
    using ConfigClientFn = std::function<void()>;
    using ConfigExitFn = std::function<void()>;
    using NtpApplyFn = std::function<bool(time_t utc_epoch)>;

    WifiManager();
    ~WifiManager();

    void start();
    void set_config_callbacks(ConfigEnterFn on_enter, ConfigClientFn on_client, ConfigExitFn on_exit);
    void set_ntp_apply_callback(NtpApplyFn fn);

    bool enter_config_mode();
    void stop_config_ap();
    void request_ntp_sync();

    bool is_config_active() const { return state_ == WifiManagerState::ConfigAp; }
    WifiManagerState state() const { return state_; }

    void get_ap_status(WifiApStatus *out) const;
    bool set_sta_credentials(const char *ssid, const char *password);
    bool clear_sta_credentials();
    bool load_sta_credentials(WifiStaCredentials *out) const;

    void touch_http_activity();

private:
    enum class Command : uint8_t
    {
        EnterConfig,
        StopConfig,
        RunNtpSync,
        ApClientConnected,
    };

    struct CommandMsg
    {
        Command cmd;
    };

    static void task_entry(void *param);
    static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                   void *event_data);
    static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                 void *event_data);

    void run();
    bool ensure_wifi_init();
    bool start_config_ap();
    void deauth_all_clients();
    void shutdown_wifi();
    esp_netif_t *ensure_ap_netif();
    void handle_config_timeout();
    void handle_idle_timeout();
    bool run_sta_ntp_sync_blocking();
    void process_command(const CommandMsg &msg);
    void generate_session();
    void exit_config_ap_internal(bool invoke_callback);

    TaskHandle_t task_handle_;
    mutable portMUX_TYPE status_mux_;
    WifiManagerState state_;
    uint16_t session_code_;
    char config_ssid_[32];
    TickType_t config_deadline_;
    TickType_t idle_deadline_;
    TickType_t last_http_activity_;
    bool idle_timer_active_;
    uint8_t client_count_;
    uint16_t client_aids_[4];
    uint8_t client_aid_count_;
    bool handlers_registered_;
    esp_netif_t *ap_netif_;
    SemaphoreHandle_t ntp_done_sem_;

    ConfigEnterFn on_config_enter_;
    ConfigClientFn on_config_client_;
    ConfigExitFn on_config_exit_;
    NtpApplyFn on_ntp_apply_;

    QueueHandle_t command_queue_;

    static constexpr const char *kApPass = "nixie2026";
    static constexpr uint32_t kConfigDurationSec = 15 * 60;
    static constexpr uint32_t kIdleAfterDisconnectSec = 120;
    static constexpr uint32_t kStaConnectTimeoutMs = 30000;
    static constexpr uint32_t kNtpTimeoutMs = 30000;
};
