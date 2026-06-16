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

Bq25601::Bq25601(i2c_port_t port, uint8_t address)
    : port_(port),
      address_(address)
{
}

bool Bq25601::init()
{
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

    // 2) Set VAC OVP to 14.2V
    if (!set_vac_ovp_mv(14200)) {
        ESP_LOGE(TAG, "Failed to set VAC OVP to 14.2V");
        return false;
    }

    // 3) Enable charging at boot
    if (!enable_charging()) {
        ESP_LOGE(TAG, "Failed to enable charging at boot");
        return false;
    }

    ESP_LOGI(TAG, "Init done: Ichg=1.6A, VACOVP=14.2V, charging default");
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
    data.vac_ovp_mv = static_cast<uint16_t>(10500 + ((reg06 & kReg06VacOvpMask) >> 6) * 1200);

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

bool Bq25601::set_vac_ovp_mv(uint16_t ovp_mv)
{
    uint8_t code = encode_vac_ovp(ovp_mv);
    return update_reg_bits(kReg06, kReg06VacOvpMask, static_cast<uint8_t>(code << 6));
}

bool Bq25601::read_reg(uint8_t reg, uint8_t *val)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool Bq25601::write_reg(uint8_t reg, uint8_t val)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
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
    // First-version approximation: 10.5V + code*1.2V
    if (ovp_mv <= 10500) return 0;
    uint16_t code = static_cast<uint16_t>((ovp_mv - 10500) / 1200);
    if (code > 0x03) code = 0x03;
    return static_cast<uint8_t>(code);
}
