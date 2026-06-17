#include "gasgauge_service.h"

GasgaugeService::GasgaugeService(IGasgaugeDriver &driver)
    : driver_(driver)
{
}

bool GasgaugeService::probe_device_info(GasgaugeDeviceInfo &info)
{
    return driver_.probe(info);
}

bool GasgaugeService::configure_capacity(uint16_t mah, bool force)
{
    return driver_.configure(mah, force);
}

bool GasgaugeService::read_data(GasgaugeData &data)
{
    return driver_.get_data(data);
}

bool GasgaugeService::is_ready() const
{
    return driver_.is_ready();
}

bool GasgaugeService::peek_registers(uint8_t reg, uint8_t *out, size_t len)
{
    return driver_.peek_registers(reg, out, len);
}

bool GasgaugeService::dump_state_block(uint8_t class_id, uint8_t block_index,
                                       uint8_t out[32], uint8_t *checksum)
{
    return driver_.dump_state_block(class_id, block_index, out, checksum);
}
