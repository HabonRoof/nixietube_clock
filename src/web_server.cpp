#include "web_server.h"
#include "system_controller.h"
#include "system_state.h"
#include "web_page.h"
#include "daemons/audio_daemon.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "nvs_flash.h"
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

    char alarm_time[16];
    snprintf(alarm_time, sizeof(alarm_time), "%02u:%02u:%02u",
             settings.alarm_hour, settings.alarm_minute, settings.alarm_second);

    char rgb[16];
    snprintf(rgb, sizeof(rgb), "%u,%u,%u", settings.backlight_r, settings.backlight_g, settings.backlight_b);

    char response[288];
    snprintf(response, sizeof(response),
             "{\"tz_offset\":%d,\"alarm_enabled\":%s,\"alarm_time\":\"%s\",\"backlight_rgb\":\"%s\",\"backlight_brightness\":%u,\"backlight_effect\":%u,\"volume\":%u}",
             settings.tz_offset_hours,
             settings.alarm_enabled ? "true" : "false",
             alarm_time,
             rgb,
             settings.backlight_brightness,
             settings.backlight_effect,
             settings.volume);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
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

    char response[288];
    snprintf(response, sizeof(response),
             "{\"local_time\":\"%s\",\"unix_utc\":%lld,\"tz_offset\":%d,\"time_valid\":%s,\"osf\":%s,\"temperature\":%.2f}",
             local_str,
             (long long)unix_utc,
             settings.tz_offset_hours,
             time_valid ? "true" : "false",
             osf ? "true" : "false",
             temperature);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static bool parse_time(const char *text, struct tm *out_tm)
{
    if (!text || !out_tm) {
        return false;
    }

    int year, month, day, hour, minute, second;
    if (sscanf(text, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }

    out_tm->tm_year = year - 1900;
    out_tm->tm_mon = month - 1;
    out_tm->tm_mday = day;
    out_tm->tm_hour = hour;
    out_tm->tm_min = minute;
    out_tm->tm_sec = second;
    out_tm->tm_wday = 0;
    return true;
}

static bool parse_alarm(const char *text, uint8_t *h, uint8_t *m, uint8_t *s)
{
    if (!text || !h || !m || !s) {
        return false;
    }
    int hour, minute, second;
    if (sscanf(text, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    *h = static_cast<uint8_t>(hour);
    *m = static_cast<uint8_t>(minute);
    *s = static_cast<uint8_t>(second);
    return true;
}

static bool parse_rgb(const char *text, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!text || !r || !g || !b) {
        return false;
    }
    int rr, gg, bb;
    if (sscanf(text, "%d,%d,%d", &rr, &gg, &bb) != 3) {
        return false;
    }
    if (rr < 0 || rr > 255 || gg < 0 || gg > 255 || bb < 0 || bb > 255) {
        return false;
    }
    *r = static_cast<uint8_t>(rr);
    *g = static_cast<uint8_t>(gg);
    *b = static_cast<uint8_t>(bb);
    return true;
}

static std::string extract_json_value(const std::string &json, const char *key)
{
    std::string pattern = std::string("\"") + key + "\"";
    size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }
    size_t colon = json.find(':', key_pos);
    if (colon == std::string::npos) {
        return {};
    }
    size_t start = json.find_first_not_of(" \t\n\r", colon + 1);
    if (start == std::string::npos) {
        return {};
    }
    if (json[start] == '"') {
        size_t end = json.find('"', start + 1);
        if (end == std::string::npos) {
            return {};
        }
        return json.substr(start + 1, end - start - 1);
    }
    size_t end = json.find_first_of(",}\n\r", start);
    if (end == std::string::npos) {
        end = json.size();
    }
    return json.substr(start, end - start);
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
        AudioDaemonStatus st = {};
        server->audio_daemon().snapshot_status(&st);

        char response[192];
        const char *err = "query_failed";
        if (!st.dfplayer_init_ok) {
            err = "init_failed";
        } else if (!st.sd_card_online) {
            err = "sd_not_online";
        }

        snprintf(response, sizeof(response),
                 "{\"count\":0,\"tracks\":[],\"error\":\"%s\",\"dfplayer_init\":%s,"
                 "\"sd_online\":%s,\"device_mask\":%u}",
                 err,
                 st.dfplayer_init_ok ? "true" : "false",
                 st.sd_card_online ? "true" : "false",
                 st.device_mask);

        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }

    std::string response = "{\"count\":" + std::to_string(count) + ",\"tracks\":[";
    for (uint16_t i = 1; i <= count; ++i) {
        if (i > 1) {
            response += ',';
        }
        char name[16];
        snprintf(name, sizeof(name), "%04u.mp3", i);
        response += "{\"id\":" + std::to_string(i) + ",\"name\":\"" + name + "\"}";
    }
    response += "]}";

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response.c_str(), response.size());
}

static esp_err_t audio_status_get_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    AudioDaemonStatus status = {};
    if (!server->audio_daemon().rpc_get_status(&status)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"track\":0,\"state\":\"stopped\",\"error\":\"status_failed\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    char response[160];
    snprintf(response, sizeof(response),
             "{\"track\":%u,\"state\":\"%s\",\"count\":%u,"
             "\"dfplayer_init\":%s,\"sd_online\":%s,\"device_mask\":%u}",
             status.current_track,
             audio_state_string(status.state),
             status.track_count,
             status.dfplayer_init_ok ? "true" : "false",
             status.sd_card_online ? "true" : "false",
             status.device_mask);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t audio_play_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    std::string body;
    body.resize(req->content_len);
    int received = httpd_req_recv(req, body.data(), body.size());
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        return ESP_FAIL;
    }

    std::string track_value = extract_json_value(body, "track");
    if (track_value.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing track");
        return ESP_FAIL;
    }

    int track = std::stoi(track_value);
    if (track < 1 || track > 9999) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid track");
        return ESP_FAIL;
    }

    AudioDaemonStatus status = {};
    if (!server->audio_daemon().rpc_toggle_track(static_cast<uint16_t>(track), &status)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Toggle failed");
        return ESP_FAIL;
    }

    char response[96];
    snprintf(response, sizeof(response),
             "{\"track\":%u,\"state\":\"%s\"}",
             status.current_track,
             audio_state_string(status.state));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    auto *server = static_cast<WebServer *>(req->user_ctx);
    std::string body;
    body.resize(req->content_len);
    int received = httpd_req_recv(req, body.data(), body.size());
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
        return ESP_FAIL;
    }

    ClockSettings settings;
    if (!server->load_settings(&settings)) {
        settings = SystemState::defaults();
    }

    std::string tz_value = extract_json_value(body, "tz_offset");
    if (!tz_value.empty()) {
        settings.tz_offset_hours = static_cast<int8_t>(std::stoi(tz_value));
    }

    std::string alarm_enabled = extract_json_value(body, "alarm_enabled");
    if (!alarm_enabled.empty()) {
        settings.alarm_enabled = (alarm_enabled == "true" || alarm_enabled == "1");
    }

    std::string alarm_time = extract_json_value(body, "alarm_time");
    if (!alarm_time.empty()) {
        uint8_t h, m, s;
        if (parse_alarm(alarm_time.c_str(), &h, &m, &s)) {
            settings.alarm_hour = h;
            settings.alarm_minute = m;
            settings.alarm_second = s;
        }
    }

    std::string rgb = extract_json_value(body, "backlight_rgb");
    if (!rgb.empty()) {
        uint8_t r, g, b;
        if (parse_rgb(rgb.c_str(), &r, &g, &b)) {
            settings.backlight_r = r;
            settings.backlight_g = g;
            settings.backlight_b = b;
        }
    }

    std::string brightness = extract_json_value(body, "backlight_brightness");
    if (!brightness.empty()) {
        int value = std::stoi(brightness);
        if (value >= 0 && value <= 255) {
            settings.backlight_brightness = static_cast<uint8_t>(value);
        }
    }

    std::string effect = extract_json_value(body, "backlight_effect");
    if (!effect.empty()) {
        int value = std::stoi(effect);
        if (value >= 0 && value <= 3) {
            settings.backlight_effect = static_cast<uint8_t>(value);
        }
    }

    std::string volume = extract_json_value(body, "volume");
    if (!volume.empty()) {
        int value = std::stoi(volume);
        if (value >= 0 && value <= 30) {
            settings.volume = static_cast<uint8_t>(value);
        }
    }

    std::string time_value = extract_json_value(body, "time");
    struct tm timeinfo = {};
    bool has_time = parse_time(time_value.c_str(), &timeinfo);

    if (!server->apply_settings(settings, has_time ? &timeinfo : nullptr)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to apply settings");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
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

bool WebServer::get_time_status(struct tm *local_out, bool *time_valid, bool *osf,
                                float *temperature, time_t *unix_utc)
{
    return system_controller_.get_time_status(local_out, time_valid, osf, temperature, unix_utc);
}

AudioDaemon &WebServer::audio_daemon()
{
    return audio_daemon_;
}
