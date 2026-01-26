#pragma once

#include "gasgauge_driver.h"
#include "driver/i2c.h"

class Bq27441 : public IGasgaugeDriver
{
public:
    Bq27441(i2c_port_t port);
    virtual ~Bq27441() = default;

    bool init() override;
    bool get_data(GasgaugeData &data) override;

private:
    bool configure_battery(uint16_t capacity_mah);
    bool unseal();
    bool seal();
    bool enter_config_mode();
    bool exit_config_mode();
    bool set_design_capacity(uint16_t capacity_mah);
    bool get_design_capacity(uint16_t *capacity_mah);
    bool control_command(uint16_t subcommand);
    
    bool read_word(uint8_t reg, uint16_t *val);
    bool write_word(uint8_t reg, uint16_t val);
    bool read_block(uint8_t reg, uint8_t *data, size_t len);
    bool write_block_data(uint8_t class_id, uint8_t offset, uint8_t *data, uint8_t len);
    bool checksum(uint8_t *check_sum);

    i2c_port_t port_;
    static constexpr uint8_t kAddress = 0x55; // Default I2C address for BQ27441
};