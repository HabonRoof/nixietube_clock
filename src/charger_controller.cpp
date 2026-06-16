#include "charger_controller.h"

#include "esp_log.h"

static const char *TAG = "ChargerController";

ChargerController::ChargerController(IChargerDriver &driver)
    : driver_(driver)
{
}

bool ChargerController::init()
{
    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize charger driver");
        return false;
    }
    return true;
}

bool ChargerController::read_status_register(uint8_t &status)
{
    return driver_.read_status_register(status);
}

bool ChargerController::read_power_on_config_register(uint8_t &reg01)
{
    return driver_.read_power_on_config_register(reg01);
}

bool ChargerController::enable_charging()
{
    return driver_.enable_charging();
}

bool ChargerController::disable_charging()
{
    return driver_.disable_charging();
}

bool ChargerController::set_charge_current_ma(uint16_t current_ma)
{
    return driver_.set_charge_current_ma(current_ma);
}
