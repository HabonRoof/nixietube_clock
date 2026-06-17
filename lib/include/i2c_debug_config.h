#pragma once

// Temporary I2C isolation flags for BQ27441 debugging.
namespace i2c_debug {

static constexpr bool kDisablePca9685I2c = false;
static constexpr bool kDisableDs3231Rtc = false;
static constexpr bool kDisableGasgauge = false;

// SystemController battery poll interval (firmware-only)
static constexpr uint32_t kBatteryPollMs = 60000;

} // namespace i2c_debug
