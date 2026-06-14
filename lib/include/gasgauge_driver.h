#pragma once

#include <cstddef>
#include <cstdint>

struct GasgaugeData {
    uint16_t voltage_mv;
    int16_t current_ma;
    uint8_t soc;
    uint8_t soh;
};

struct GasgaugeDeviceInfo {
    uint16_t device_type;
    uint16_t fw_version;
    uint16_t design_capacity;
    uint16_t control_status;
    uint16_t flags;
    bool sealed;
    bool battery_detected;
    bool init_complete;
    bool needs_reconfig;
    bool probed;
};

class IGasgaugeDriver
{
public:
    virtual ~IGasgaugeDriver() = default;

    virtual bool probe(GasgaugeDeviceInfo &info) = 0;
    virtual bool configure(uint16_t capacity_mah, bool force = false) = 0;
    virtual bool get_data(GasgaugeData &data) = 0;
    virtual bool is_ready() const = 0;

    virtual bool peek_registers(uint8_t reg, uint8_t *out, size_t len) = 0;
    virtual bool dump_state_block(uint8_t class_id, uint8_t block_index,
                                  uint8_t out[32], uint8_t *checksum) = 0;
};
