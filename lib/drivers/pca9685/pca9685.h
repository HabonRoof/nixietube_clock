#pragma once

#include <cstdint>
#include <cstddef>
#include "driver/i2c.h"
#include "driver/gpio.h"

class Pca9685
{
public:
    Pca9685(i2c_port_t port, uint8_t address);

    static bool init_i2c(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t i2c_clock_hz);
    bool init(uint32_t i2c_clock_hz, float pwm_frequency_hz);
    bool set_pwm(uint8_t channel, uint16_t on, uint16_t off);
    bool set_duty(uint8_t channel, uint16_t duty);
    bool set_all_off();

private:
    bool write_register(uint8_t reg, uint8_t value);
    bool write_registers(uint8_t reg, const uint8_t *data, size_t length);

    i2c_port_t port_;
    uint8_t address_;
};
