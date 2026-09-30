#include "power_controller.h"

#include "husb238/husb238.h"

#include "esp_log.h"

#include <cstdio>

static const char *TAG = "PowerController";

PowerController::PowerController(IPowerSwitchDriver &driver, Husb238 *pd_sink)
    : driver_(driver),
      pd_sink_(pd_sink),
      pd_ok_(false)
{
    snprintf(pd_status_, sizeof(pd_status_), "HUSB238: 12V PDO not requested, HV boost off");
}

bool PowerController::init()
{
    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize power switch driver");
        publish_pd_status(false, "HUSB238: power switch init failed, HV boost left off");
        return false;
    }
    return true;
}

bool PowerController::bring_up_hv()
{
    if (!set_hv_enabled(false)) {
        publish_pd_status(false, "HUSB238: failed to hold HV boost off");
        return false;
    }

    if (pd_sink_ == nullptr) {
        publish_pd_status(false, "HUSB238: driver missing, HV boost left off");
        return false;
    }

    Husb238Contract contract;
    const Husb238RequestResult result = pd_sink_->request_12v(contract);
    if (result != Husb238RequestResult::Success) {
        char message[sizeof(pd_status_)];
        snprintf(message, sizeof(message),
                 "HUSB238: 12V PDO failed (%s, now %s, %s), HV boost left off",
                 Husb238::result_name(result),
                 Husb238::voltage_name(contract.voltage),
                 Husb238::response_name(contract.response));
        publish_pd_status(false, message);
        return false;
    }

    if (!set_hv_enabled(true)) {
        publish_pd_status(false, "HUSB238: 12V PDO success, but HV boost failed to enable");
        return false;
    }

    char message[sizeof(pd_status_)];
    snprintf(message, sizeof(message),
             "HUSB238: 12V PDO success (%s), HV boost enabled",
             Husb238::current_name(contract.current_code));
    publish_pd_status(true, message);
    return true;
}

bool PowerController::set_hv_enabled(bool enabled)
{
    if (!driver_.set_hv_enabled(enabled)) {
        ESP_LOGW(TAG, "Failed to set HV rail: %s", enabled ? "enabled" : "disabled");
        return false;
    }
    return true;
}

bool PowerController::set_dfplayer_enabled(bool enabled)
{
    if (!driver_.set_dfplayer_enabled(enabled)) {
        ESP_LOGW(TAG, "Failed to set DFPlayer power: %s", enabled ? "enabled" : "disabled");
        return false;
    }
    return true;
}

void PowerController::publish_pd_status(bool ok, const char *message)
{
    pd_ok_ = ok;
    snprintf(pd_status_, sizeof(pd_status_), "%s", message);
    printf("%s\n", pd_status_);
    if (ok) {
        ESP_LOGI(TAG, "%s", pd_status_);
    } else {
        ESP_LOGW(TAG, "%s", pd_status_);
    }
}
