#pragma once

#include <cstdint>

struct ChargerData {
    bool power_good;
    bool charging_enabled;

    uint16_t charge_current_limit_ma;
    uint16_t vac_ovp_mv;

    uint8_t charge_state; // raw/decoded from IC status register
    uint8_t vbus_state;   // raw/decoded from IC status register
    uint8_t fault_raw;
};

class IChargerDriver
{
public:
    virtual ~IChargerDriver() = default;

    virtual bool init() = 0;
    virtual bool get_data(ChargerData &data) = 0;
    virtual bool read_status_register(uint8_t &status) = 0;
    virtual bool read_power_on_config_register(uint8_t &reg01) = 0;

    virtual bool enable_charging() = 0;
    virtual bool disable_charging() = 0;
    virtual bool set_charge_current_ma(uint16_t current_ma) = 0;
};

