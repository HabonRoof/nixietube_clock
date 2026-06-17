#pragma once

#include <cstddef>
#include <cstdint>
#include "gasgauge_driver.h"

class GasgaugeService
{
public:
    static constexpr uint16_t kDefaultCapacityMah = 3600;

    explicit GasgaugeService(IGasgaugeDriver &driver);

    bool probe_device_info(GasgaugeDeviceInfo &info);
    bool configure_capacity(uint16_t mah, bool force = false);
    bool read_data(GasgaugeData &data);
    bool is_ready() const;
    bool peek_registers(uint8_t reg, uint8_t *out, size_t len);
    bool dump_state_block(uint8_t class_id, uint8_t block_index,
                          uint8_t out[32], uint8_t *checksum);

private:
    IGasgaugeDriver &driver_;
};
