#include "web_json.h"

#include <cstring>
#include <string>

namespace {
esp_err_t send_simple_error(httpd_req_t *req, const char *status, const char *code,
                            const char *message)
{
    return send_json_error(req, status, code, nullptr, message);
}

bool key_allowed(const char *key, std::initializer_list<const char *> allowed)
{
    if (!key) {
        return false;
    }
    for (const char *candidate : allowed) {
        if (std::strcmp(key, candidate) == 0) {
            return true;
        }
    }
    return false;
}
}

bool json_object_has_only_keys(const cJSON *object, std::initializer_list<const char *> allowed,
                               JsonFieldError *error)
{
    if (!cJSON_IsObject(object)) {
        if (error) {
            error->code = "invalid_type";
            error->field = "";
            error->message = "root must be an object";
        }
        return false;
    }
    for (const cJSON *item = object->child; item; item = item->next) {
        if (!key_allowed(item->string, allowed)) {
            if (error) {
                error->code = "unknown_field";
                error->field = item->string ? item->string : "";
                error->message = "field is not supported";
            }
            return false;
        }
        for (const cJSON *other = item->next; other; other = other->next) {
            if (item->string && other->string && std::strcmp(item->string, other->string) == 0) {
                if (error) {
                    error->code = "duplicate_field";
                    error->field = item->string;
                    error->message = "field must not appear more than once";
                }
                return false;
            }
        }
    }
    return true;
}

esp_err_t receive_json_request(httpd_req_t *req, CJsonPtr *root)
{
    if (!req || !root) {
        return ESP_FAIL;
    }
    if (req->content_len == 0) {
        send_simple_error(req, "400 Bad Request", "empty_body", "request body is empty");
        return ESP_FAIL;
    }
    if (req->content_len > kMaxJsonRequestBytes) {
        send_simple_error(req, "413 Payload Too Large", "payload_too_large",
                          "JSON request exceeds 4096 bytes");
        return ESP_FAIL;
    }

    const size_t content_length = req->content_len;
    std::string body(content_length + 1, '\0');
    size_t total = 0;
    while (total < content_length) {
        const int received = httpd_req_recv(req, body.data() + total, content_length - total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            send_simple_error(req, "400 Bad Request", "body_read_failed",
                              "failed to read complete request body");
            return ESP_FAIL;
        }
        total += static_cast<size_t>(received);
    }

    CJsonPtr parsed(cJSON_ParseWithLengthOpts(body.data(), total, nullptr, true));
    if (!parsed) {
        send_simple_error(req, "400 Bad Request", "invalid_json", "malformed JSON document");
        return ESP_FAIL;
    }
    *root = std::move(parsed);
    return ESP_OK;
}

esp_err_t send_json_response(httpd_req_t *req, const cJSON *root, const char *status)
{
    if (!req || !root) {
        return ESP_FAIL;
    }
    char *encoded = cJSON_PrintUnformatted(root);
    if (!encoded) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON serialization failed");
        return ESP_FAIL;
    }

    if (status) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json");
    const esp_err_t result = httpd_resp_send(req, encoded, HTTPD_RESP_USE_STRLEN);
    cJSON_free(encoded);
    return result;
}

esp_err_t send_json_ok(httpd_req_t *req)
{
    CJsonPtr root(cJSON_CreateObject());
    if (!root || !cJSON_AddBoolToObject(root.get(), "ok", true)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_FAIL;
    }
    return send_json_response(req, root.get());
}

esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *code,
                          const char *field, const char *message)
{
    CJsonPtr root(cJSON_CreateObject());
    cJSON *error = root ? cJSON_AddObjectToObject(root.get(), "error") : nullptr;
    if (!root || !cJSON_AddBoolToObject(root.get(), "ok", false) || !error ||
        !cJSON_AddStringToObject(error, "code", code ? code : "unknown_error") ||
        (field && !cJSON_AddStringToObject(error, "field", field)) ||
        !cJSON_AddStringToObject(error, "message", message ? message : "request failed")) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
        return ESP_FAIL;
    }
    return send_json_response(req, root.get(), status);
}
