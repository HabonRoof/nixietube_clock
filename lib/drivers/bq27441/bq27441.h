#pragma once

#include "gasgauge_driver.h"
#include "driver/i2c.h"

class Bq27441 : public IGasgaugeDriver
{
public:
    explicit Bq27441(i2c_port_t port);
    ~Bq27441() override = default;

    bool probe(GasgaugeDeviceInfo &info) override;
    bool configure(uint16_t capacity_mah, bool force = false) override;
    bool get_data(GasgaugeData &data) override;
    bool is_ready() const override;

    bool peek_registers(uint8_t reg, uint8_t *out, size_t len) override;
    bool dump_state_block(uint8_t class_id, uint8_t block_index,
                          uint8_t out[32], uint8_t *checksum) override;

private:
    static uint8_t checksum_replace(uint8_t old_csum,
                                    const uint8_t *old_bytes,
                                    const uint8_t *new_bytes,
                                    size_t len);

    bool read_design_capacity(uint16_t *capacity_mah);
    bool read_control_status(uint16_t *status);
    bool read_flags(uint16_t *flags);
    bool is_sealed();
    bool unseal();
    bool seal();
    bool enter_config_mode();
    bool exit_config_mode();
    bool select_block(uint8_t class_id, uint8_t block_index);
    bool read_block_buffer(uint8_t out[32], uint8_t *checksum);
    bool write_design_capacity_mah(uint16_t old_capacity_mah, uint16_t new_capacity_mah);

    bool control_subcommand_read(uint16_t subcommand, uint16_t *result);
    bool control_command(uint16_t subcommand);

    esp_err_t i2c_read_byte(uint8_t reg, uint8_t *val);
    esp_err_t i2c_write_byte(uint8_t reg, uint8_t val);
    esp_err_t i2c_read_word(uint8_t reg, uint16_t *val);
    esp_err_t i2c_write_word(uint8_t reg, uint16_t val);
    esp_err_t i2c_read_bytes(uint8_t reg, uint8_t *buf, size_t len);

    i2c_port_t port_;
    bool ready_ = false;
};
