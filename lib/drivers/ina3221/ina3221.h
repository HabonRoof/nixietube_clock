#pragma once

#include "powermonitor_driver.h"
#include "driver/i2c.h"

class Ina3221 : public IPowerMonitorDriver
{
public:
    Ina3221(i2c_port_t port, uint8_t address = 0x40);
    virtual ~Ina3221() = default;

    bool init() override;
    bool get_data(PowerMonitorData &data) override;

private:
    bool write_register(uint8_t reg, uint16_t val);
    bool read_register(uint8_t reg, uint16_t *val);
    
    int16_t get_shunt_voltage(uint8_t channel);
    int16_t get_bus_voltage(uint8_t channel);
    
    // Helper to calculate current and power
    // We need shunt resistor values. 
    // Assuming standard values or configurable? 
    // For now I'll assume some defaults or add setters if needed.
    // Usually 0.1 Ohm or similar. 
    // Let's add a method to set shunt resistance if needed, or assume 0.1 Ohm for now.
    // Actually, better to pass shunt values in constructor or init?
    // Let's stick to simple for now and maybe define constants in cpp or h.
    
    float shunt_resistor_mv_ma_[3]; // Shunt resistance in mOhm? No, usually Ohm. 
                                    // Let's store in Ohms.
                                    
    i2c_port_t port_;
    uint8_t address_;
};