#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "gasgauge_driver.h"

struct GasgaugeSnapshot {
    GasgaugeData data;
    bool valid;
    TickType_t updated_at;
};

class SystemState
{
public:
    SystemState();

    void update_gasgauge(const GasgaugeData &data);
    bool get_gasgauge(GasgaugeSnapshot *out) const;

    void set_device_info(const GasgaugeDeviceInfo &info);
    bool get_device_info(GasgaugeDeviceInfo *out) const;

private:
    mutable portMUX_TYPE mux_;
    GasgaugeSnapshot gasgauge_;
    GasgaugeDeviceInfo device_info_;
};
