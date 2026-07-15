#include "web_server.h"
#include "system_controller.h"
#include "system_state.h"
#include "settings_json.h"
#include "web_json.h"
#include "web_page.h"
#include "message_types.h"
#include "daemons/audio_daemon.h"
#include "dfplayer_mini.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "nvs_flash.h"
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

namespace {
static const char *kTag = "WebServer";

constexpr const char *kApSsid = "NixieClock";
constexpr const char *kApPass = "nixie2026";
constexpr uint32_t kMaxConn = 2;


httpd_handle_t g_http = nullptr;


static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    ClockSettings settings;
    if (!server->load_settings(&settings)) {
        settings = SystemState::defaults();
    }

    CJsonPtr response(settings_to_json(settings));
    if (!response) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build settings response");
    }
    return send_json_response(req, response.get());
}

static esp_err_t time_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);

    struct tm local_tm = {};
    bool time_valid = false;
    bool osf = false;
    float temperature = 0.0f;
    time_t unix_utc = 0;
    server->get_time_status(&local_tm, &time_valid, &osf, &temperature, &unix_utc);

    ClockSettings settings;
    if (!server->load_settings(&settings)) {
        settings = SystemState::defaults();
    }

    char local_str[80];
    snprintf(local_str, sizeof(local_str), "%04d-%02d-%02d %02d:%02d:%02d",
             local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
             local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

    CJsonPtr response(cJSON_CreateObject());
    cJSON *clock = response ? cJSON_AddObjectToObject(response.get(), "clock") : nullptr;
    if (!response || !clock ||
        !cJSON_AddStringToObject(response.get(), "local_time", local_str) ||
        !cJSON_AddNumberToObject(response.get(), "unix_utc", static_cast<double>(unix_utc)) ||
        !cJSON_AddBoolToObject(response.get(), "time_valid", time_valid) ||
        !cJSON_AddBoolToObject(response.get(), "osf", osf) ||
        !cJSON_AddNumberToObject(response.get(), "temperature", temperature) ||
        !cJSON_AddNumberToObject(clock, "timezone_offset_hours", settings.tz_offset_hours) ||
        !cJSON_AddBoolToObject(clock, "rtc_calibrated", settings.rtc_calibrated)) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build time response");
    }
    return send_json_response(req, response.get());
}

static const char *audio_state_string(AudioPlaybackUiState state)
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

static esp_err_t audio_tracks_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    uint16_t count = 0;
    if (!server->audio_daemon().rpc_query_tracks(&count)) {
        CJsonPtr response(cJSON_CreateObject());
        if (!response || !cJSON_AddNumberToObject(response.get(), "count", 0) ||
            !cJSON_AddArrayToObject(response.get(), "tracks") ||
            !cJSON_AddStringToObject(response.get(), "error", "query_failed")) {
            return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                                   "failed to build audio response");
        }
        return send_json_response(req, response.get());
    }

    CJsonPtr response(cJSON_CreateObject());
    cJSON *tracks = response ? cJSON_AddArrayToObject(response.get(), "tracks") : nullptr;
    if (!response || !tracks || !cJSON_AddStringToObject(response.get(), "folder", "mp3") ||
        !cJSON_AddNumberToObject(response.get(), "count", count)) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build audio response");
    }
    for (uint16_t i = 1; i <= count; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "mp3/%04u.mp3", i);
        cJSON *track = cJSON_CreateObject();
        if (!track || !cJSON_AddNumberToObject(track, "id", i) ||
            !cJSON_AddStringToObject(track, "name", name)) {
            cJSON_Delete(track);
            return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                                   "failed to build audio response");
        }
        cJSON_AddItemToArray(tracks, track);
    }
    return send_json_response(req, response.get());
}

static esp_err_t audio_status_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    AudioDaemonStatus status = {};
    if (!server->audio_daemon().rpc_get_status(&status)) {
        CJsonPtr response(cJSON_CreateObject());
        if (!response || !cJSON_AddNumberToObject(response.get(), "track", 0) ||
            !cJSON_AddStringToObject(response.get(), "state", "stopped") ||
            !cJSON_AddStringToObject(response.get(), "error", "status_failed")) {
            return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                                   "failed to build audio response");
        }
        return send_json_response(req, response.get());
    }

    CJsonPtr response(cJSON_CreateObject());
    if (!response || !cJSON_AddNumberToObject(response.get(), "track", status.current_track) ||
        !cJSON_AddStringToObject(response.get(), "state", audio_state_string(status.state)) ||
        !cJSON_AddNumberToObject(response.get(), "count", status.track_count)) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build audio response");
    }
    return send_json_response(req, response.get());
}

static esp_err_t audio_play_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    CJsonPtr request;
    if (receive_json_request(req, &request) != ESP_OK) {
        return ESP_FAIL;
    }
    JsonFieldError field_error;
    if (!json_object_has_only_keys(request.get(), {"track"}, &field_error)) {
        send_json_error(req, "400 Bad Request", field_error.code, field_error.field,
                        field_error.message);
        return ESP_FAIL;
    }
    const cJSON *track_value = cJSON_GetObjectItemCaseSensitive(request.get(), "track");
    if (!track_value) {
        send_json_error(req, "400 Bad Request", "missing_field", "track", "track is required");
        return ESP_FAIL;
    }
    if (!cJSON_IsNumber(track_value) || std::floor(track_value->valuedouble) != track_value->valuedouble ||
        track_value->valuedouble < kDfPlayerMp3MinFile ||
        track_value->valuedouble > kDfPlayerMp3MaxFile) {
        send_json_error(req, "400 Bad Request", "invalid_field", "track",
                        "track must be an integer in the supported DFPlayer range");
        return ESP_FAIL;
    }
    const int track = static_cast<int>(track_value->valuedouble);

    AudioDaemonStatus status = {};
    if (!server->audio_daemon().rpc_toggle_track(static_cast<uint16_t>(track), &status)) {
        send_json_error(req, "500 Internal Server Error", "toggle_failed", nullptr,
                        "failed to toggle audio track");
        return ESP_FAIL;
    }
    CJsonPtr response(cJSON_CreateObject());
    if (!response || !cJSON_AddNumberToObject(response.get(), "track", status.current_track) ||
        !cJSON_AddStringToObject(response.get(), "state", audio_state_string(status.state))) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build audio response");
    }
    return send_json_response(req, response.get());
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    CJsonPtr request;
    if (receive_json_request(req, &request) != ESP_OK) {
        return ESP_FAIL;
    }

    ClockSettings current;
    if (!server->load_settings(&current)) {
        current = SystemState::defaults();
    }
    ParsedSettingsUpdate update = {};
    SettingsJsonError error;
    if (!parse_settings_update(request.get(), current, &update, &error)) {
        send_json_error(req, "400 Bad Request", error.code.c_str(),
                        error.field.empty() ? nullptr : error.field.c_str(), error.message.c_str());
        return ESP_FAIL;
    }
    if (!server->apply_settings_update(update)) {
        send_json_error(req, "500 Internal Server Error", "apply_failed", nullptr,
                        "failed to apply settings");
        return ESP_FAIL;
    }
    return send_json_ok(req);
}
}

WebServer::WebServer(SystemController &system_controller, SystemState &system_state,
                     AudioDaemon &audio_daemon)
    : system_controller_(system_controller),
      system_state_(system_state),
      audio_daemon_(audio_daemon),
      task_handle_(nullptr)
{
}

WebServer::~WebServer()
{
    stop();
}

void WebServer::start()
{
    if (!task_handle_) {
        xTaskCreate(task_entry, "web_server", 8192, this, 5, &task_handle_);
    }
}

void WebServer::stop()
{
    stop_http();
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

void WebServer::task_entry(void *param)
{
    auto *server = static_cast<WebServer *>(param);
    server->run();
}

void WebServer::run()
{
    ESP_LOGI(kTag, "Starting AP web server");
    start_ap();
    start_http();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool WebServer::start_ap()
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(kTag, "Failed to create AP netif");
        return false;
    }
    esp_netif_ip_info_t ip_info = {};
    esp_netif_str_to_ip4("192.168.8.8", &ip_info.ip);
    esp_netif_str_to_ip4("192.168.8.8", &ip_info.gw);
    esp_netif_str_to_ip4("255.255.255.0", &ip_info.netmask);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap_config = {};
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), kApSsid, sizeof(ap_config.ap.ssid));
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), kApPass, sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = std::strlen(kApSsid);
    ap_config.ap.channel = 1;
    ap_config.ap.max_connection = kMaxConn;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (std::strlen(kApPass) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(kTag, "AP started: SSID=%s channel=%u auth=%s", kApSsid, ap_config.ap.channel,
             ap_config.ap.authmode == WIFI_AUTH_OPEN ? "OPEN" : "WPA/WPA2");
    return true;
}

bool WebServer::start_http()
{
    if (g_http) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 8192;

    if (httpd_start(&g_http, &config) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start HTTP server");
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t settings_get = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = settings_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t settings_post = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = settings_post_handler,
        .user_ctx = this,
    };

    httpd_uri_t time_get = {
        .uri = "/api/time",
        .method = HTTP_GET,
        .handler = time_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t audio_tracks_get = {
        .uri = "/api/audio/tracks",
        .method = HTTP_GET,
        .handler = audio_tracks_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t audio_status_get = {
        .uri = "/api/audio/status",
        .method = HTTP_GET,
        .handler = audio_status_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t audio_play_post = {
        .uri = "/api/audio/play",
        .method = HTTP_POST,
        .handler = audio_play_post_handler,
        .user_ctx = this,
    };

    httpd_register_uri_handler(g_http, &index_uri);
    httpd_register_uri_handler(g_http, &settings_get);
    httpd_register_uri_handler(g_http, &settings_post);
    httpd_register_uri_handler(g_http, &time_get);
    httpd_register_uri_handler(g_http, &audio_tracks_get);
    httpd_register_uri_handler(g_http, &audio_status_get);
    httpd_register_uri_handler(g_http, &audio_play_post);
    return true;
}

void WebServer::stop_http()
{
    if (g_http) {
        httpd_stop(g_http);
        g_http = nullptr;
    }
}

bool WebServer::load_settings(ClockSettings *out_settings)
{
    return system_state_.get_settings(out_settings);
}

bool WebServer::apply_settings(const ClockSettings &settings, const struct tm *new_time)
{
    system_controller_.request_settings_update(settings, new_time);
    return true;
}

bool WebServer::apply_settings_update(const ParsedSettingsUpdate &update)
{
    SettingsUpdate msg = {};
    msg.settings = update.settings;
    msg.local_time = update.local_time;
    msg.has_time = update.has_local_time;
    msg.persist = update.persist;
    msg.cancel_preview = update.cancel_preview;
    msg.preview_only = update.preview_only;
    msg.volume_preview = update.volume_preview;
    msg.preview_profile = update.preview_profile;
    system_controller_.request_settings_update(msg);
    return true;
}

bool WebServer::get_time_status(struct tm *local_out, bool *time_valid, bool *osf,
                                float *temperature, time_t *unix_utc)
{
    return system_controller_.get_time_status(local_out, time_valid, osf, temperature, unix_utc);
}

AudioDaemon &WebServer::audio_daemon()
{
    return audio_daemon_;
}
