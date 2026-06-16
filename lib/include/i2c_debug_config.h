#pragma once

// Temporary I2C isolation flags for BQ27441 debugging.
namespace i2c_debug {

static constexpr bool kDisablePca9685I2c = false;
static constexpr bool kDisableDs3231Rtc = true;
static constexpr bool kDisableGasgauge = true;

// GasgaugeDaemon timing (firmware-only, not BQ27441 hardware registers)
static constexpr uint32_t kGasgaugePollMs = 5000;
static constexpr uint32_t kGasgaugeProbeRetryMs = 5000; // was 30000; shorten for debug

} // namespace i2c_debug
