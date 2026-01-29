#pragma once

#include <cstdint>

struct PowerChannelData {
    uint16_t voltage_mv;
    int16_t current_ma;
    int32_t power_mw;
};

struct PowerMonitorData {
    PowerChannelData hv;       // Channel 1: HV Power
    PowerChannelData charging; // Channel 2: 5V Charging Power
    PowerChannelData led;      // Channel 3: LED Backlight Power
};

class IPowerMonitorDriver
{
public:
    virtual ~IPowerMonitorDriver() = default;

    virtual bool init() = 0;
    virtual bool get_data(PowerMonitorData &data) = 0;
};