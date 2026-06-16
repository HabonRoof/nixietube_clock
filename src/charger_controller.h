#pragma once

#include <cstdint>
#include "charger_driver.h"

class ChargerController
{
public:
    explicit ChargerController(IChargerDriver &driver);

    bool init();

    bool read_status_register(uint8_t &status);
    bool read_power_on_config_register(uint8_t &reg01);

    bool enable_charging();
    bool disable_charging();
    bool set_charge_current_ma(uint16_t current_ma);

private:
    IChargerDriver &driver_;
};
