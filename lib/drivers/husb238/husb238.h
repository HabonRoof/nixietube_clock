#pragma once

#include "driver/i2c.h"

#include <cstdint>

// HUSB238 register map (Hynetek Register Information rev 1.1).
// I2C slave address is 0x08. PDO_SELECT codes are not the same as PD_STATUS0 voltage codes.

static constexpr uint8_t kHusb238Address = 0x08;

enum class Husb238Voltage : uint8_t {
    Unattached = 0x0,
    V5 = 0x1,
    V9 = 0x2,
    V12 = 0x3,
    V15 = 0x4,
    V18 = 0x5,
    V20 = 0x6,
};

// SRC_PDO (0x08) bits [7:4]. 15V/18V/20V are not sequential.
enum class Husb238PdoSelect : uint8_t {
    None = 0x0,
    V5 = 0x1,
    V9 = 0x2,
    V12 = 0x3,
    V15 = 0x8,
    V18 = 0x9,
    V20 = 0xA,
};

enum class Husb238Response : uint8_t {
    NoResponse = 0x0,
    Success = 0x1,
    Invalid = 0x3,
    NotSupported = 0x4,
    TransactionFail = 0x5,
};

enum class Husb238RequestResult : uint8_t {
    Success,
    I2cFail,
    NotAttached,
    NotAdvertised,
    Rejected,
    Timeout,
};

struct Husb238Contract {
    bool i2c_ok = false;
    bool attached = false;
    bool pdo_12v_advertised = false;
    Husb238Voltage voltage = Husb238Voltage::Unattached;
    Husb238Response response = Husb238Response::NoResponse;
    uint8_t current_code = 0;
};

class Husb238
{
public:
    explicit Husb238(i2c_port_t port, uint8_t address = kHusb238Address);

    bool read_contract(Husb238Contract &contract);
    Husb238RequestResult request_12v(Husb238Contract &contract);

    static const char *voltage_name(Husb238Voltage voltage);
    static const char *current_name(uint8_t current_code);
    static const char *response_name(Husb238Response response);
    static const char *result_name(Husb238RequestResult result);

private:
    bool read_reg(uint8_t reg, uint8_t *val);
    bool write_reg(uint8_t reg, uint8_t val);
    bool wait_until(bool (Husb238::*predicate)(Husb238Contract &), Husb238Contract &contract, uint32_t timeout_ms);
    bool attached(Husb238Contract &contract);
    bool twelve_volt_advertised(Husb238Contract &contract);
    bool twelve_volt_contract(Husb238Contract &contract);
    Husb238RequestResult request_12v_once(Husb238Contract &contract);

    i2c_port_t port_;
    uint8_t address_;
};
