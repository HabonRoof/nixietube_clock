#pragma once

// Temporary I2C isolation flags for BQ27441 debugging.
namespace i2c_debug {

static constexpr bool kDisablePca9685I2c = false;
static constexpr bool kDisableDs3231Rtc = false;
static constexpr bool kDisableGasgauge = false;
static constexpr bool kDisableALS = false;

// Dev convenience: when running without a VBAT battery, the DS3231 oscillator
// stops on every power-down, so the OSF (oscillator stop flag) is set at every
// boot and the time is never trusted. With this enabled, the SystemController
// seeds the RTC with the firmware build time (which also clears OSF) instead of
// waiting forever for the user to set the clock. Keep false in production.
static constexpr bool kSeedRtcOnOscStop = true;

// SystemController battery poll interval (firmware-only)
static constexpr uint32_t kBatteryPollMs = 60000;

} // namespace i2c_debug
