#pragma once

#include "esp_log.h"
#include <cstdint>

// #region agent log
static constexpr const char *kDbgSession = "b91f53";

static inline void dbg_session_log(const char *hypothesis_id,
                                   const char *location,
                                   const char *message,
                                   int32_t a = -1,
                                   int32_t b = -1,
                                   int32_t c = -1)
{
    ESP_LOGI("DBG-b91f53",
             "{\"sessionId\":\"%s\",\"hypothesisId\":\"%s\",\"location\":\"%s\","
             "\"message\":\"%s\",\"data\":{\"a\":%ld,\"b\":%ld,\"c\":%ld},"
             "\"timestamp\":%llu}",
             kDbgSession, hypothesis_id, location, message,
             static_cast<long>(a), static_cast<long>(b), static_cast<long>(c),
             static_cast<unsigned long long>(esp_log_timestamp()));
}
// #endregion
