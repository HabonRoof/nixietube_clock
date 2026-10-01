#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_sntp.h"
#include <cstring>
#include <cmath>

namespace {
constexpr const char *kTag = "WifiManager";

bool wifi_call_ok(esp_err_t err, const char *what)
{
    if (err == ESP_OK) {
        return true;
    }
    ESP_LOGE(kTag, "%s failed: %s", what, esp_err_to_name(err));
    return false;
}

bool configure_ap_address(esp_netif_t *ap_netif)
{
    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_str_to_ip4("192.168.8.8", &ip_info.ip) != ESP_OK ||
        esp_netif_str_to_ip4("192.168.8.8", &ip_info.gw) != ESP_OK ||
        esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask) != ESP_OK) {
        ESP_LOGE(kTag, "Invalid config AP address");
        return false;
    }

    esp_err_t err = esp_netif_dhcps_stop(ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        return wifi_call_ok(err, "esp_netif_dhcps_stop");
    }
    if (!wifi_call_ok(esp_netif_set_ip_info(ap_netif, &ip_info), "esp_netif_set_ip_info")) {
        return false;
    }
    err = esp_netif_dhcps_start(ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return wifi_call_ok(err, "esp_netif_dhcps_start");
    }
    return true;
}

portMUX_TYPE g_event_mux = portMUX_INITIALIZER_UNLOCKED;
WifiManager *g_wifi_manager = nullptr;

void sntp_sync_callback(struct timeval *tv)
{
    if (g_wifi_manager) {
        // Handled via semaphore in run_sta_ntp_sync_blocking
    }
    (void)tv;
}
} // namespace

WifiManager::WifiManager()
    : task_handle_(nullptr),
      status_mux_(portMUX_INITIALIZER_UNLOCKED),
      state_(WifiManagerState::Off),
      session_code_(0),
      config_deadline_(0),
      idle_deadline_(0),
      last_http_activity_(0),
      idle_timer_active_(false),
      client_count_(0),
      client_aid_count_(0),
      handlers_registered_(false),
      ap_netif_(nullptr),
      sta_netif_(nullptr),
      ap_stop_handler_(nullptr),
      sta_stop_handler_(nullptr),
      wifi_stopped_sem_(nullptr),
      ntp_done_sem_(nullptr),
      command_queue_(nullptr)
{
    config_ssid_[0] = '\0';
}

WifiManager::~WifiManager()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (command_queue_) {
        vQueueDelete(command_queue_);
        command_queue_ = nullptr;
    }
    if (ap_stop_handler_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_AP_STOP, ap_stop_handler_);
        ap_stop_handler_ = nullptr;
    }
    if (sta_stop_handler_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_STOP, sta_stop_handler_);
        sta_stop_handler_ = nullptr;
    }
    if (wifi_stopped_sem_) {
        vSemaphoreDelete(wifi_stopped_sem_);
        wifi_stopped_sem_ = nullptr;
    }
    if (ntp_done_sem_) {
        vSemaphoreDelete(ntp_done_sem_);
        ntp_done_sem_ = nullptr;
    }
    g_wifi_manager = nullptr;
}

void WifiManager::start()
{
    if (task_handle_) {
        return;
    }
    command_queue_ = xQueueCreate(4, sizeof(CommandMsg));
    wifi_stopped_sem_ = xSemaphoreCreateBinary();
    ntp_done_sem_ = xSemaphoreCreateBinary();
    if (!command_queue_ || !wifi_stopped_sem_ || !ntp_done_sem_) {
        ESP_LOGE(kTag, "Failed to create WiFi manager queues");
        return;
    }
    g_wifi_manager = this;
    xTaskCreate(task_entry, "wifi_mgr", 8192, this, 4, &task_handle_);
}

void WifiManager::set_config_callbacks(ConfigEnterFn on_enter, ConfigClientFn on_client,
                                       ConfigExitFn on_exit)
{
    on_config_enter_ = std::move(on_enter);
    on_config_client_ = std::move(on_client);
    on_config_exit_ = std::move(on_exit);
}

void WifiManager::set_ntp_apply_callback(NtpApplyFn fn)
{
    on_ntp_apply_ = std::move(fn);
}

bool WifiManager::enter_config_mode()
{
    if (!command_queue_) {
        return false;
    }
    CommandMsg msg = {Command::EnterConfig};
    if (xQueueSend(command_queue_, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(kTag, "Config AP command dropped");
        return false;
    }
    return true;
}

void WifiManager::stop_config_ap()
{
    if (!command_queue_) {
        return;
    }
    CommandMsg msg = {Command::StopConfig};
    if (xQueueSend(command_queue_, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(kTag, "Config AP stop command dropped");
    }
}

void WifiManager::request_ntp_sync()
{
    if (!command_queue_) {
        return;
    }
    CommandMsg msg = {Command::RunNtpSync};
    xQueueSend(command_queue_, &msg, pdMS_TO_TICKS(100));
}

void WifiManager::touch_http_activity()
{
    portENTER_CRITICAL(&status_mux_);
    last_http_activity_ = xTaskGetTickCount();
    portEXIT_CRITICAL(&status_mux_);
}

bool WifiManager::set_sta_credentials(const char *ssid, const char *password)
{
    return wifi_credentials_save(ssid, password);
}

bool WifiManager::clear_sta_credentials()
{
    return wifi_credentials_clear();
}

bool WifiManager::load_sta_credentials(WifiStaCredentials *out) const
{
    return wifi_credentials_load(out);
}

void WifiManager::get_ap_status(WifiApStatus *out) const
{
    if (!out) {
        return;
    }
    std::memset(out, 0, sizeof(*out));

    portENTER_CRITICAL(&status_mux_);
    out->active = state_ == WifiManagerState::ConfigAp;
    out->session_code = session_code_;
    out->client_count = client_count_;
    std::strncpy(out->ssid, config_ssid_, sizeof(out->ssid));
    std::strncpy(out->password, kApPass, sizeof(out->password));
    if (out->active && config_deadline_ > 0) {
        const TickType_t now = xTaskGetTickCount();
        if (config_deadline_ > now) {
            out->remaining_sec =
                static_cast<int>(pdTICKS_TO_MS(config_deadline_ - now) / 1000);
        }
    }
    portEXIT_CRITICAL(&status_mux_);
}

void WifiManager::task_entry(void *param)
{
    auto *mgr = static_cast<WifiManager *>(param);
    mgr->run();
}

void WifiManager::wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                     void *event_data)
{
    auto *mgr = static_cast<WifiManager *>(arg);
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED: {
        const auto *ev = static_cast<const wifi_event_ap_staconnected_t *>(event_data);
        portENTER_CRITICAL(&g_event_mux);
        if (mgr->client_aid_count_ < sizeof(mgr->client_aids_) / sizeof(mgr->client_aids_[0])) {
            mgr->client_aids_[mgr->client_aid_count_++] = ev->aid;
        }
        mgr->client_count_++;
        mgr->idle_timer_active_ = false;
        portEXIT_CRITICAL(&g_event_mux);
        ESP_LOGI(kTag, "AP client connected aid=%u total=%u", ev->aid, mgr->client_count_);
        if (mgr->command_queue_) {
            const CommandMsg defer = {Command::ApClientConnected};
            xQueueSend(mgr->command_queue_, &defer, 0);
        }
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        const auto *ev = static_cast<const wifi_event_ap_stadisconnected_t *>(event_data);
        portENTER_CRITICAL(&g_event_mux);
        if (mgr->client_count_ > 0) {
            mgr->client_count_--;
        }
        for (uint8_t i = 0; i < mgr->client_aid_count_; ++i) {
            if (mgr->client_aids_[i] == ev->aid) {
                mgr->client_aids_[i] = mgr->client_aids_[--mgr->client_aid_count_];
                break;
            }
        }
        if (mgr->client_count_ == 0 && mgr->state_ == WifiManagerState::ConfigAp) {
            mgr->idle_timer_active_ = true;
            mgr->idle_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kIdleAfterDisconnectSec * 1000);
        }
        portEXIT_CRITICAL(&g_event_mux);
        ESP_LOGI(kTag, "AP client disconnected aid=%u total=%u", ev->aid, mgr->client_count_);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED:
        xSemaphoreGive(mgr->ntp_done_sem_);
        break;
    default:
        break;
    }
}

void WifiManager::ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                   void *event_data)
{
    auto *mgr = static_cast<WifiManager *>(arg);
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(kTag, "STA got IP");
        xSemaphoreGive(mgr->ntp_done_sem_);
    }
    (void)event_data;
}

bool WifiManager::ensure_wifi_init()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return wifi_call_ok(err, "esp_netif_init");
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return wifi_call_ok(err, "esp_event_loop_create_default");
    }

    if (!handlers_registered_) {
        esp_event_handler_instance_t wifi_inst = nullptr;
        esp_event_handler_instance_t ip_inst = nullptr;
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &WifiManager::wifi_event_handler, this, &wifi_inst);
        if (err != ESP_OK) {
            return wifi_call_ok(err, "WIFI_EVENT handler register");
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &WifiManager::ip_event_handler, this, &ip_inst);
        if (err != ESP_OK) {
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_inst);
            return wifi_call_ok(err, "IP_EVENT handler register");
        }
        handlers_registered_ = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return wifi_call_ok(err, "esp_wifi_init");
    }

    // Pairing credentials are ephemeral. Keeping them out of the Wi-Fi driver's
    // NVS avoids a flash write while the nixie scan is running.
    return wifi_call_ok(esp_wifi_set_storage(WIFI_STORAGE_RAM), "esp_wifi_set_storage(RAM)");
}

void WifiManager::generate_session()
{
    session_code_ = static_cast<uint16_t>(esp_random() % 10000);
    snprintf(config_ssid_, sizeof(config_ssid_), "NixieClock-%04u", session_code_);
}

esp_netif_t *WifiManager::ensure_ap_netif()
{
    if (ap_netif_) {
        return ap_netif_;
    }

    ap_netif_ = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif_) {
        return ap_netif_;
    }

    ap_netif_ = esp_netif_create_default_wifi_ap();
    return ap_netif_;
}

esp_netif_t *WifiManager::ensure_sta_netif()
{
    if (sta_netif_) {
        return sta_netif_;
    }

    sta_netif_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif_) {
        return sta_netif_;
    }

    sta_netif_ = esp_netif_create_default_wifi_sta();
    return sta_netif_;
}

void WifiManager::wifi_stopped_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                       void *event_data)
{
    (void)event_base;
    (void)event_id;
    (void)event_data;
    auto *mgr = static_cast<WifiManager *>(arg);
    if (mgr->wifi_stopped_sem_) {
        xSemaphoreGive(mgr->wifi_stopped_sem_);
    }
}

bool WifiManager::install_stop_handlers_last()
{
    if (ap_stop_handler_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_AP_STOP, ap_stop_handler_);
        ap_stop_handler_ = nullptr;
    }
    if (sta_stop_handler_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_STOP, sta_stop_handler_);
        sta_stop_handler_ = nullptr;
    }

    esp_err_t err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_AP_STOP, &WifiManager::wifi_stopped_handler, this, &ap_stop_handler_);
    if (!wifi_call_ok(err, "AP stop handler register")) {
        return false;
    }
    err = esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_STOP, &WifiManager::wifi_stopped_handler, this, &sta_stop_handler_);
    if (!wifi_call_ok(err, "STA stop handler register")) {
        return false;
    }
    return true;
}

bool WifiManager::await_wifi_stopped()
{
    if (!wifi_stopped_sem_) {
        ESP_LOGE(kTag, "WiFi stop semaphore missing");
        return false;
    }

    const bool handlers_ok = install_stop_handlers_last();
    xSemaphoreTake(wifi_stopped_sem_, 0);

    const esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT) {
        return true;
    }
    if (!wifi_call_ok(err, "esp_wifi_stop")) {
        return false;
    }
    if (!handlers_ok) {
        ESP_LOGE(kTag, "WiFi stop was requested but the stop event was not observed");
        return false;
    }
    if (xSemaphoreTake(wifi_stopped_sem_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(kTag, "Timed out waiting for WiFi stop");
        return false;
    }
    return true;
}

bool WifiManager::start_config_ap()
{
    if (!ensure_wifi_init()) {
        ESP_LOGE(kTag, "Config AP cancelled");
        return false;
    }

    generate_session();

    esp_netif_t *ap_netif = ensure_ap_netif();
    if (!ap_netif) {
        shutdown_wifi();
        ESP_LOGE(kTag, "Config AP cancelled: AP netif missing");
        return false;
    }

    if (!configure_ap_address(ap_netif)) {
        shutdown_wifi();
        ESP_LOGE(kTag, "Config AP cancelled");
        return false;
    }

    wifi_config_t ap_config = {};
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), config_ssid_,
                 sizeof(ap_config.ap.ssid));
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), kApPass,
                 sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = static_cast<uint8_t>(std::strlen(config_ssid_));
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = 2;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.dtim_period = 1;

    if (!wifi_call_ok(esp_wifi_set_mode(WIFI_MODE_AP), "esp_wifi_set_mode(AP)") ||
        !wifi_call_ok(esp_wifi_set_config(WIFI_IF_AP, &ap_config), "esp_wifi_set_config(AP)") ||
        !wifi_call_ok(esp_wifi_start(), "esp_wifi_start(AP)")) {
        shutdown_wifi();
        ESP_LOGE(kTag, "Config AP cancelled");
        return false;
    }

    client_count_ = 0;
    client_aid_count_ = 0;
    idle_timer_active_ = false;
    config_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(kConfigDurationSec * 1000);
    last_http_activity_ = xTaskGetTickCount();
    state_ = WifiManagerState::ConfigAp;

    ESP_LOGI(kTag, "Config AP started SSID=%s pass=%s code=%04u", config_ssid_, kApPass,
             session_code_);

    if (on_config_enter_) {
        on_config_enter_(session_code_);
    }
    return true;
}

void WifiManager::deauth_all_clients()
{
    uint16_t aids[4] = {};
    uint8_t aid_count = 0;

    portENTER_CRITICAL(&g_event_mux);
    aid_count = client_aid_count_;
    for (uint8_t i = 0; i < aid_count; ++i) {
        aids[i] = client_aids_[i];
    }
    portEXIT_CRITICAL(&g_event_mux);

    for (uint8_t i = 0; i < aid_count; ++i) {
        esp_wifi_deauth_sta(aids[i]);
    }
}

bool WifiManager::shutdown_wifi()
{
    const bool stopped = await_wifi_stopped();
    if (!stopped) {
        ESP_LOGE(kTag, "WiFi radio was not confirmed stopped");
    }
    state_ = WifiManagerState::Off;
    client_count_ = 0;
    client_aid_count_ = 0;
    idle_timer_active_ = false;
    return stopped;
}

void WifiManager::exit_config_ap_internal(bool invoke_callback)
{
    if (state_ != WifiManagerState::ConfigAp) {
        return;
    }
    deauth_all_clients();
    vTaskDelay(pdMS_TO_TICKS(100));
    if (shutdown_wifi()) {
        ESP_LOGI(kTag, "Config AP stopped");
    }
    if (invoke_callback && on_config_exit_) {
        on_config_exit_();
    }
}

void WifiManager::handle_config_timeout()
{
    ESP_LOGI(kTag, "Config AP session timeout");
    exit_config_ap_internal(true);
}

void WifiManager::handle_idle_timeout()
{
    ESP_LOGI(kTag, "Config AP idle timeout (no clients)");
    exit_config_ap_internal(true);
}

bool WifiManager::run_sta_ntp_sync_blocking()
{
    WifiStaCredentials creds = {};
    if (!wifi_credentials_load(&creds) || !creds.configured) {
        ESP_LOGI(kTag, "NTP skipped: no STA credentials");
        return false;
    }

    if (state_ == WifiManagerState::ConfigAp) {
        ESP_LOGW(kTag, "NTP deferred: config AP active");
        return false;
    }

    if (!ensure_wifi_init()) {
        return false;
    }

    if (!ensure_sta_netif()) {
        ESP_LOGE(kTag, "NTP cancelled: STA netif missing");
        return false;
    }

    wifi_config_t sta_config = {};
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.ssid), creds.ssid,
                 sizeof(sta_config.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(sta_config.sta.password), creds.password,
                 sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    if (!wifi_call_ok(esp_wifi_set_mode(WIFI_MODE_STA), "esp_wifi_set_mode(STA)") ||
        !wifi_call_ok(esp_wifi_set_config(WIFI_IF_STA, &sta_config), "esp_wifi_set_config(STA)") ||
        !wifi_call_ok(esp_wifi_start(), "esp_wifi_start(STA)")) {
        shutdown_wifi();
        ESP_LOGE(kTag, "NTP cancelled");
        return false;
    }

    xSemaphoreTake(ntp_done_sem_, 0);
    state_ = WifiManagerState::StaNtpSync;
    if (!wifi_call_ok(esp_wifi_connect(), "esp_wifi_connect")) {
        shutdown_wifi();
        wifi_ntp_status_save(0, false);
        ESP_LOGE(kTag, "NTP cancelled");
        return false;
    }

    if (xSemaphoreTake(ntp_done_sem_, pdMS_TO_TICKS(kStaConnectTimeoutMs)) != pdTRUE) {
        ESP_LOGW(kTag, "STA connect timeout");
        shutdown_wifi();
        wifi_ntp_status_save(0, false);
        return false;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_callback);
    esp_sntp_init();

    bool ntp_ok = false;
    time_t utc_now = 0;
    for (int i = 0; i < 20; ++i) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&utc_now);
        if (utc_now > 1700000000) {
            ntp_ok = true;
            break;
        }
    }
    esp_sntp_stop();

    const esp_err_t disc = esp_wifi_disconnect();
    if (disc != ESP_OK && disc != ESP_ERR_WIFI_NOT_STARTED && disc != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(kTag, "esp_wifi_disconnect: %s", esp_err_to_name(disc));
    }
    shutdown_wifi();

    if (!ntp_ok) {
        ESP_LOGW(kTag, "NTP sync timeout");
        wifi_ntp_status_save(0, false);
        return false;
    }

    if (on_ntp_apply_) {
        if (!on_ntp_apply_(utc_now)) {
            wifi_ntp_status_save(utc_now, false);
            return false;
        }
    }

    ESP_LOGI(kTag, "NTP sync OK utc=%lld", static_cast<long long>(utc_now));
    wifi_ntp_status_save(utc_now, true);
    return true;
}

void WifiManager::process_command(const CommandMsg &msg)
{
    switch (msg.cmd) {
    case Command::EnterConfig:
        if (state_ == WifiManagerState::ConfigAp) {
            exit_config_ap_internal(true);
        }
        if (state_ == WifiManagerState::StaNtpSync) {
            shutdown_wifi();
        }
        start_config_ap();
        break;
    case Command::StopConfig:
        exit_config_ap_internal(true);
        break;
    case Command::RunNtpSync:
        run_sta_ntp_sync_blocking();
        break;
    case Command::ApClientConnected:
        if (on_config_client_) {
            on_config_client_();
        }
        break;
    }
}

void WifiManager::run()
{
    ESP_LOGI(kTag, "WifiManager started");
    CommandMsg msg = {};

    while (true) {
        if (xQueueReceive(command_queue_, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            process_command(msg);
        }

        if (state_ == WifiManagerState::ConfigAp) {
            const TickType_t now = xTaskGetTickCount();
            if (config_deadline_ > 0 && (int32_t)(now - config_deadline_) >= 0) {
                handle_config_timeout();
            } else if (idle_timer_active_ && client_count_ == 0 &&
                       (int32_t)(now - idle_deadline_) >= 0) {
                handle_idle_timeout();
            }
        }
    }
}
