#pragma once

#include <cstdint>
#include <ctime>
#include "driver/i2c.h"
#include "driver/gpio.h"

class Ds3231
{
public:
    Ds3231(i2c_port_t port, uint8_t address = 0x68);

    bool init();
    bool get_time(struct tm *timeinfo);
    bool set_time(const struct tm *timeinfo);
    bool get_temperature(float *temp);
    
    // Alarm functions
    // Alarm 1 support
    bool set_alarm1(const struct tm *timeinfo);
    bool clear_alarm1_flag();
    bool enable_alarm1_interrupt(bool enable);

private:
    uint8_t bcd2dec(uint8_t val);
    uint8_t dec2bcd(uint8_t val);
    bool read_register(uint8_t reg, uint8_t *val);
    bool write_register(uint8_t reg, uint8_t val);
    bool read_registers(uint8_t reg, uint8_t *data, size_t len);
    bool write_registers(uint8_t reg, const uint8_t *data, size_t len);

    i2c_port_t port_;
    uint8_t address_;
};