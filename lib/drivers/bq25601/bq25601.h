#pragma once

#include "charger_driver.h"
#include "driver/i2c.h"

class Bq25601 : public IChargerDriver
{
public:
    explicit Bq25601(i2c_port_t port, uint8_t address = 0x6B);
    virtual ~Bq25601() = default;

    bool init() override;
    bool get_data(ChargerData &data) override;
    bool read_status_register(uint8_t &status) override;
    bool read_power_on_config_register(uint8_t &reg01) override;

    bool enable_charging() override;
    bool disable_charging() override;
    bool set_charge_current_ma(uint16_t current_ma) override;

    bool enable_otg() override;
    bool disable_otg() override;
    bool set_otg_voltage_mv(uint16_t voltage_mv) override;

    bool set_vac_ovp_mv(uint16_t ovp_mv);

private:
    bool read_reg(uint8_t reg, uint8_t *val);
    bool write_reg(uint8_t reg, uint8_t val);
    bool update_reg_bits(uint8_t reg, uint8_t mask, uint8_t value);
    bool reset_watchdog_timer();

    uint8_t encode_ichg(uint16_t current_ma) const;
    uint8_t encode_votg(uint16_t voltage_mv) const;
    uint8_t encode_vac_ovp(uint16_t ovp_mv) const;

    i2c_port_t port_;
    uint8_t address_;
};
