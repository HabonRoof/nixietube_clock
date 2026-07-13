#pragma once

#include "cJSON.h"
#include "esp_http_server.h"
#include <initializer_list>
#include <memory>

struct CJsonDeleter {
    void operator()(cJSON *value) const
    {
        cJSON_Delete(value);
    }
};

using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

constexpr size_t kMaxJsonRequestBytes = 4096;

esp_err_t receive_json_request(httpd_req_t *req, CJsonPtr *root);
esp_err_t send_json_response(httpd_req_t *req, const cJSON *root,
                             const char *status = nullptr);
esp_err_t send_json_ok(httpd_req_t *req);
esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *code,
                          const char *field, const char *message);

struct JsonFieldError {
    const char *code;
    const char *field;
    const char *message;
};

bool json_object_has_only_keys(const cJSON *object, std::initializer_list<const char *> allowed,
                               JsonFieldError *error = nullptr);
