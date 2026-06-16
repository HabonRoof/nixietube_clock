#include "power_controller.h"

#include "esp_log.h"

static const char *TAG = "PowerController";

PowerController::PowerController(IPowerSwitchDriver &driver)
    : driver_(driver)
{
}

bool PowerController::init()
{
    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize power switch driver");
        return false;
    }
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
