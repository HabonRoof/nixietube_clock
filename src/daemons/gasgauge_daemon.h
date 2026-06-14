#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gasgauge_driver.h"
#include "system_state.h"
#include "i2c_debug_config.h"

class GasgaugeDaemon
{
public:
    static constexpr TickType_t kPollIntervalMs = i2c_debug::kGasgaugePollMs;
    static constexpr TickType_t kRetryIntervalMs = i2c_debug::kGasgaugeProbeRetryMs;
    static constexpr uint8_t kMaxReadFailures = 3;
    static constexpr uint16_t kDefaultCapacityMah = 3600;

    GasgaugeDaemon(IGasgaugeDriver &driver, SystemState &system_state);
    ~GasgaugeDaemon();

    void start();

    bool probe_device_info(GasgaugeDeviceInfo &info);
    bool configure_capacity(uint16_t mah, bool force = false);
    bool read_live(GasgaugeData &data);
    bool get_cached_snapshot(GasgaugeSnapshot &snapshot) const;
    bool peek_registers(uint8_t reg, uint8_t *out, size_t len);
    bool dump_state_block(uint8_t class_id, uint8_t block_index,
                          uint8_t out[32], uint8_t *checksum);

private:
    static void task_entry(void *param);
    void loop();
    bool ensure_probed();

    IGasgaugeDriver &driver_;
    SystemState &system_state_;
    TaskHandle_t task_handle_;
};
