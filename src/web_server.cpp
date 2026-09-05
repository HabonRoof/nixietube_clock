#include "web_server.h"
#include "wifi_manager.h"
#include "wifi_credentials.h"
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
    server->wifi_manager().touch_http_activity();
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

static esp_err_t ap_status_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    server->wifi_manager().touch_http_activity();
    WifiApStatus status = {};
    server->wifi_manager().get_ap_status(&status);

    CJsonPtr response(cJSON_CreateObject());
    if (!response || !cJSON_AddBoolToObject(response.get(), "active", status.active) ||
        !cJSON_AddNumberToObject(response.get(), "session_code", status.session_code) ||
        !cJSON_AddStringToObject(response.get(), "ssid", status.ssid) ||
        !cJSON_AddStringToObject(response.get(), "password", status.password) ||
        !cJSON_AddNumberToObject(response.get(), "remaining_sec", status.remaining_sec) ||
        !cJSON_AddNumberToObject(response.get(), "client_count", status.client_count)) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build ap status response");
    }
    return send_json_response(req, response.get());
}

static esp_err_t ap_stop_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    server->wifi_manager().touch_http_activity();
    server->wifi_manager().stop_config_ap();
    return send_json_ok(req);
}

static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    server->wifi_manager().touch_http_activity();
    WifiStaCredentials creds = {};
    server->wifi_manager().load_sta_credentials(&creds);

    CJsonPtr response(cJSON_CreateObject());
    if (!response || !cJSON_AddBoolToObject(response.get(), "configured", creds.configured) ||
        !cJSON_AddStringToObject(response.get(), "ssid",
                                 creds.configured ? creds.ssid : "")) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build wifi response");
    }
    return send_json_response(req, response.get());
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    server->wifi_manager().touch_http_activity();
    CJsonPtr request;
    if (receive_json_request(req, &request) != ESP_OK) {
        return ESP_FAIL;
    }

    const cJSON *ssid_value = cJSON_GetObjectItemCaseSensitive(request.get(), "ssid");
    const cJSON *pass_value = cJSON_GetObjectItemCaseSensitive(request.get(), "password");
    if (!cJSON_IsString(ssid_value) || ssid_value->valuestring[0] == '\0') {
        send_json_error(req, "400 Bad Request", "missing_field", "ssid", "ssid is required");
        return ESP_FAIL;
    }
    const char *password = cJSON_IsString(pass_value) ? pass_value->valuestring : "";

    if (!server->wifi_manager().set_sta_credentials(ssid_value->valuestring, password)) {
        send_json_error(req, "500 Internal Server Error", "save_failed", nullptr,
                        "failed to save wifi credentials");
        return ESP_FAIL;
    }
    return send_json_ok(req);
}

static esp_err_t wifi_delete_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    server->wifi_manager().touch_http_activity();
    if (!server->wifi_manager().clear_sta_credentials()) {
        send_json_error(req, "500 Internal Server Error", "clear_failed", nullptr,
                        "failed to clear wifi credentials");
        return ESP_FAIL;
    }
    return send_json_ok(req);
}

static esp_err_t ntp_status_get_handler(httpd_req_t *req)
{
    WifiNtpStatus status = {};
    wifi_ntp_status_load(&status);

    CJsonPtr response(cJSON_CreateObject());
    if (!response || !cJSON_AddBoolToObject(response.get(), "configured", status.configured) ||
        !cJSON_AddBoolToObject(response.get(), "last_success", status.last_ntp_success) ||
        !cJSON_AddNumberToObject(response.get(), "last_ntp_unix",
                                 static_cast<double>(status.last_ntp_unix))) {
        return send_json_error(req, "500 Internal Server Error", "allocation_failed", nullptr,
                               "failed to build ntp status response");
    }
    return send_json_response(req, response.get());
}
}

WebServer::WebServer(SystemController &system_controller, SystemState &system_state,
                     AudioDaemon &audio_daemon, WifiManager &wifi_manager)
    : system_controller_(system_controller),
      system_state_(system_state),
      audio_daemon_(audio_daemon),
      wifi_manager_(wifi_manager),
      task_handle_(nullptr),
      http_running_(false)
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
    ESP_LOGI(kTag, "Web server task started (config AP on demand)");

    while (true) {
        const bool want_http = wifi_manager_.is_config_active();
        if (want_http && !http_running_) {
            if (start_http()) {
                http_running_ = true;
                ESP_LOGI(kTag, "HTTP server started");
            }
        } else if (!want_http && http_running_) {
            stop_http();
            http_running_ = false;
            ESP_LOGI(kTag, "HTTP server stopped");
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

bool WebServer::start_http()
{
    if (g_http) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
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

    httpd_uri_t ap_status_get = {
        .uri = "/api/ap/status",
        .method = HTTP_GET,
        .handler = ap_status_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t ap_stop_post = {
        .uri = "/api/ap/stop",
        .method = HTTP_POST,
        .handler = ap_stop_post_handler,
        .user_ctx = this,
    };

    httpd_uri_t wifi_get = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = wifi_get_handler,
        .user_ctx = this,
    };

    httpd_uri_t wifi_post = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = wifi_post_handler,
        .user_ctx = this,
    };

    httpd_uri_t wifi_delete = {
        .uri = "/api/wifi",
        .method = HTTP_DELETE,
        .handler = wifi_delete_handler,
        .user_ctx = this,
    };

    httpd_uri_t ntp_status_get = {
        .uri = "/api/ntp/status",
        .method = HTTP_GET,
        .handler = ntp_status_get_handler,
        .user_ctx = this,
    };

    httpd_register_uri_handler(g_http, &index_uri);
    httpd_register_uri_handler(g_http, &settings_get);
    httpd_register_uri_handler(g_http, &settings_post);
    httpd_register_uri_handler(g_http, &time_get);
    httpd_register_uri_handler(g_http, &audio_tracks_get);
    httpd_register_uri_handler(g_http, &audio_status_get);
    httpd_register_uri_handler(g_http, &audio_play_post);
    httpd_register_uri_handler(g_http, &ap_status_get);
    httpd_register_uri_handler(g_http, &ap_stop_post);
    httpd_register_uri_handler(g_http, &wifi_get);
    httpd_register_uri_handler(g_http, &wifi_post);
    httpd_register_uri_handler(g_http, &wifi_delete);
    httpd_register_uri_handler(g_http, &ntp_status_get);
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

WifiManager &WebServer::wifi_manager()
{
    return wifi_manager_;
}
