#pragma once

#include <cstdint>

struct GasgaugeData {
    uint16_t voltage_mv;
    int16_t current_ma;
    uint8_t soc; // State of Charge (%)
    uint8_t soh; // State of Health (%)
};

class IGasgaugeDriver
{
public:
    virtual ~IGasgaugeDriver() = default;

    virtual bool init() = 0;
    virtual bool get_data(GasgaugeData &data) = 0;
};