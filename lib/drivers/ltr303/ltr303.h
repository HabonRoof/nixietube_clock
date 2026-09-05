#pragma once

#include <cstdint>
#include "driver/i2c.h"

class Ltr303
{
public:
    explicit Ltr303(i2c_port_t port, uint8_t address = 0x29);

    bool init();
    bool is_ready() const { return ready_; }
    bool read_raw_lux(float *lux_out);

private:
    bool read_register(uint8_t reg, uint8_t *val);
    bool write_register(uint8_t reg, uint8_t val);
    bool read_registers(uint8_t reg, uint8_t *data, size_t len);
    float compute_lux(uint16_t ch0, uint16_t ch1) const;

    i2c_port_t port_;
    uint8_t address_;
    bool ready_ = false;
};
