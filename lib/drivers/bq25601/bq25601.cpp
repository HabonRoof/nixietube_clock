#include "bq25601.h"
#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "BQ25601";

// BQ25601 register map (subset for first version)
static constexpr uint8_t kReg00 = 0x00; // Input source control
static constexpr uint8_t kReg01 = 0x01; // Power-on config
static constexpr uint8_t kReg02 = 0x02; // Charge current
static constexpr uint8_t kReg05 = 0x05; // Charge termination / timer control (watchdog)
static constexpr uint8_t kReg06 = 0x06; // VAC OVP
static constexpr uint8_t kReg08 = 0x08; // System status
static constexpr uint8_t kReg09 = 0x09; // Fault

// REG01 bits
static constexpr uint8_t kReg01ChgConfigMask = 0x30; // [5:4]
static constexpr uint8_t kReg01ChgEnable = 0x10;
static constexpr uint8_t kReg01ChgDisable = 0x00;
static constexpr uint8_t kReg01WdtResetMask = 0x40; // [6] WDT_RESET (write 1 to reset watchdog)

// REG05 bits
static constexpr uint8_t kReg05WatchdogMask = 0x30; // [5:4] WATCHDOG (00 = disable)

// REG02 bits
static constexpr uint8_t kReg02IchgMask = 0x7F; // [6:0] for this first-version mapping

// REG06 bits
static constexpr uint8_t kReg06VacOvpMask = 0xC0; // [7:6]
// Discrete VAC OVP steps. There is no 12V code. Code 2 (10.5V) rises at
// about 10.9-11.5V and trips on a 12V PDO. Code 3 (14V, typ 14.2V) accepts 12V.
static constexpr uint8_t kVacOvpCode14V = 0x03;
static constexpr uint16_t kVacOvpFor12vContractMv = 14000;
static constexpr uint16_t kVacOvpMvByCode[] = {5500, 6500, 10500, 14000};

Bq25601::Bq25601(i2c_port_t port, uint8_t address)
    : port_(port),
      dev_(i2c_bus_add_device(port, address))
{
}

bool Bq25601::init()
{
    ovp_allows_12v_ = false;
    uint8_t status = 0;
    if (!read_reg(kReg08, &status)) {
        ESP_LOGE(TAG, "Failed to communicate with BQ25601");
        return false;
    }

    // 0) Disable watchdog so host-configured settings persist without periodic feeding.
    if (!update_reg_bits(kReg05, kReg05WatchdogMask, 0x00)) {
        ESP_LOGE(TAG, "Failed to disable watchdog");
        return false;
    }

    // 1) Set charge current to 1.6A
    if (!set_charge_current_ma(1600)) {
        ESP_LOGE(TAG, "Failed to set charge current to 1.6A");
        return false;
    }

    // 2) Raise VAC OVP before any 12V PDO request. Power loss restores the
    // 5.5V default, which trips if VBUS becomes 12V.
    if (!set_vac_ovp_mv(kVacOvpFor12vContractMv)) {
        ESP_LOGE(TAG, "VAC OVP readback failed; 12V PDO must not be requested");
        enable_charging();
        return false;
    }

    // 3) Enable charging at boot
    if (!enable_charging()) {
        ESP_LOGE(TAG, "Failed to enable charging at boot");
        return false;
    }

    ESP_LOGI(TAG, "Init done: Ichg=1.6A, VACOVP=14V confirmed, charging default");
    return true;
}

bool Bq25601::get_data(ChargerData &data)
{
    // Watchdog is disabled in init(), so host-configured settings persist without
    // periodic feeding. Reset it here anyway as a harmless no-op safeguard.
    if (!reset_watchdog_timer()) {
        ESP_LOGW(TAG, "Failed to reset BQ25601 watchdog timer");
    }

    uint8_t reg01 = 0;
    uint8_t reg02 = 0;
    uint8_t reg06 = 0;
    uint8_t reg08 = 0;
    uint8_t reg09 = 0;

    if (!read_reg(kReg01, &reg01)) return false;
    if (!read_reg(kReg02, &reg02)) return false;
    if (!read_reg(kReg06, &reg06)) return false;
    if (!read_reg(kReg08, &reg08)) return false;
    if (!read_reg(kReg09, &reg09)) return false;

    data.power_good = ((reg08 >> 2) & 0x01) != 0;
    data.charging_enabled = (reg01 & kReg01ChgConfigMask) == kReg01ChgEnable;

    data.charge_current_limit_ma = static_cast<uint16_t>(reg02 & kReg02IchgMask) * 60;
    data.vac_ovp_mv = decode_vac_ovp_mv(static_cast<uint8_t>((reg06 & kReg06VacOvpMask) >> 6));

    data.charge_state = static_cast<uint8_t>((reg08 >> 3) & 0x03);
    data.vbus_state = static_cast<uint8_t>((reg08 >> 5) & 0x07);
    data.fault_raw = reg09;

    return true;
}


bool Bq25601::read_status_register(uint8_t &status)
{
    return read_reg(kReg08, &status);
}

bool Bq25601::read_power_on_config_register(uint8_t &reg01)
{
    return read_reg(kReg01, &reg01);
}

bool Bq25601::enable_charging()
{
    return update_reg_bits(kReg01, kReg01ChgConfigMask, kReg01ChgEnable);
}

bool Bq25601::disable_charging()
{
    return update_reg_bits(kReg01, kReg01ChgConfigMask, kReg01ChgDisable);
}

bool Bq25601::set_charge_current_ma(uint16_t current_ma)
{
    uint8_t code = encode_ichg(current_ma);
    return update_reg_bits(kReg02, kReg02IchgMask, code);
}

bool Bq25601::input_ovp_allows_12v() const
{
    return ovp_allows_12v_;
}

bool Bq25601::set_vac_ovp_mv(uint16_t ovp_mv)
{
    const uint8_t code = encode_vac_ovp(ovp_mv);
    ovp_allows_12v_ = false;
    if (!update_reg_bits(kReg06, kReg06VacOvpMask, static_cast<uint8_t>(code << 6))) {
        return false;
    }

    uint8_t reg06 = 0;
    if (!read_reg(kReg06, &reg06)) {
        return false;
    }

    const uint8_t actual = static_cast<uint8_t>((reg06 & kReg06VacOvpMask) >> 6);
    if (actual != code) {
        ESP_LOGE(TAG, "VAC OVP readback mismatch: wrote %u read %u (REG06=0x%02X)",
                 code, actual, reg06);
        return false;
    }

    ovp_allows_12v_ = code == kVacOvpCode14V;
    ESP_LOGI(TAG, "VAC OVP readback REG06=0x%02X (%u mV)", reg06, decode_vac_ovp_mv(actual));
    return true;
}

bool Bq25601::read_reg(uint8_t reg, uint8_t *val)
{
    I2cBusLock lock(port_);
    return i2c_bus_read(dev_, reg, val, 1, 100) == ESP_OK;
}

bool Bq25601::write_reg(uint8_t reg, uint8_t val)
{
    I2cBusLock lock(port_);
    return i2c_bus_write(dev_, reg, &val, 1, 100) == ESP_OK;
}

bool Bq25601::update_reg_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    if (!read_reg(reg, &current)) {
        return false;
    }

    uint8_t next = static_cast<uint8_t>((current & ~mask) | (value & mask));
    if (next == current) {
        return true;
    }
    return write_reg(reg, next);
}

bool Bq25601::reset_watchdog_timer()
{
    // WDT_RESET is a write-1 pulse bit; hardware clears it automatically.
    return update_reg_bits(kReg01, kReg01WdtResetMask, kReg01WdtResetMask);
}

uint8_t Bq25601::encode_ichg(uint16_t current_ma) const
{
    // First-version approximation: 60mA/LSB, clamp into mask range.
    uint16_t code = static_cast<uint16_t>(current_ma / 60);
    if (code > kReg02IchgMask) {
        code = kReg02IchgMask;
    }
    return static_cast<uint8_t>(code);
}

uint8_t Bq25601::encode_vac_ovp(uint16_t ovp_mv) const
{
    if (ovp_mv <= kVacOvpMvByCode[0]) return 0;
    if (ovp_mv <= kVacOvpMvByCode[1]) return 1;
    if (ovp_mv <= kVacOvpMvByCode[2]) return 2;
    return kVacOvpCode14V;
}

uint16_t Bq25601::decode_vac_ovp_mv(uint8_t code)
{
    if (code > kVacOvpCode14V) {
        code = kVacOvpCode14V;
    }
    return kVacOvpMvByCode[code];
}
