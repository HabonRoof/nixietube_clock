#pragma once

#include <cstdint>

namespace bq27441 {

constexpr uint8_t kI2cAddress = 0x55;

constexpr uint8_t kRegControl = 0x00;
constexpr uint8_t kRegVoltage = 0x04;
constexpr uint8_t kRegFlags = 0x06;
constexpr uint8_t kRegAvgCurrent = 0x10;
constexpr uint8_t kRegSoc = 0x1C;
constexpr uint8_t kRegSoh = 0x20;
constexpr uint8_t kRegDesignCapacity = 0x3C;
constexpr uint8_t kRegDataClass = 0x3E;
constexpr uint8_t kRegDataBlock = 0x3F;
constexpr uint8_t kRegBlockData = 0x40;
constexpr uint8_t kRegBlockDataChecksum = 0x60;
constexpr uint8_t kRegBlockDataControl = 0x61;

constexpr uint16_t kSubCmdControlStatus = 0x0000;
constexpr uint16_t kSubCmdDeviceType = 0x0001;
constexpr uint16_t kSubCmdFwVersion = 0x0002;
constexpr uint16_t kSubCmdSetCfgupdate = 0x0013;
constexpr uint16_t kSubCmdSoftReset = 0x0042;
constexpr uint16_t kSubCmdSealed = 0x0020;
constexpr uint16_t kUnsealKey = 0x8000;

constexpr uint16_t kExpectedDeviceType = 0x0421;
constexpr uint16_t kStatusSealed = 0x2000;
constexpr uint16_t kStatusInitComp = 0x0080;
constexpr uint16_t kStatusSleep = 0x0010;
constexpr uint16_t kFlagCfgUpdate = 0x0010;
constexpr uint16_t kFlagBatDetect = 0x0008;
constexpr uint16_t kFlagITPOR = 0x0020;

constexpr uint8_t kStateClassId = 82;
constexpr uint8_t kDesignCapacityOffset = 9;
constexpr uint8_t kDesignEnergyOffset = 11;
constexpr uint8_t kStateBlockSize = 32;

} // namespace bq27441
