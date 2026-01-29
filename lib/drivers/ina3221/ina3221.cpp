#include "ina3221.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "INA3221";

// Register Definitions
static constexpr uint8_t kRegConfig = 0x00;
static constexpr uint8_t kRegShuntVoltage1 = 0x01;
static constexpr uint8_t kRegBusVoltage1 = 0x02;
static constexpr uint8_t kRegShuntVoltage2 = 0x03;
static constexpr uint8_t kRegBusVoltage2 = 0x04;
static constexpr uint8_t kRegShuntVoltage3 = 0x05;
static constexpr uint8_t kRegBusVoltage3 = 0x06;
static constexpr uint8_t kRegManufId = 0xFE;
static constexpr uint8_t kRegDieId = 0xFF;

// Configuration
// Enable all channels, Average 64 samples, VBus CT 1.1ms, VShunt CT 1.1ms, Continuous Mode
// 0111 0001 0010 0111 = 0x7127
static constexpr uint16_t kConfigValue = 0x7127;

Ina3221::Ina3221(i2c_port_t port, uint8_t address)
    : port_(port), address_(address)
{
    // Default shunt resistors (0.1 Ohm = 100 mOhm)
    // Adjust these if your hardware uses different values
    shunt_resistor_mv_ma_[0] = 0.01f;
    shunt_resistor_mv_ma_[1] = 0.01f;
    shunt_resistor_mv_ma_[2] = 0.01f;
}

bool Ina3221::init()
{
    uint16_t id;
    if (!read_register(kRegManufId, &id)) {
        ESP_LOGE(TAG, "Failed to read Manufacturer ID");
        return false;
    }
    if (id != 0x5449) { // TI ID
        ESP_LOGE(TAG, "Invalid Manufacturer ID: 0x%04X", id);
        return false;
    }

    if (!read_register(kRegDieId, &id)) {
        ESP_LOGE(TAG, "Failed to read Die ID");
        return false;
    }
    if (id != 0x3220) { // INA3221 ID
        ESP_LOGE(TAG, "Invalid Die ID: 0x%04X", id);
        return false;
    }

    // Configure
    if (!write_register(kRegConfig, kConfigValue)) {
        ESP_LOGE(TAG, "Failed to configure INA3221");
        return false;
    }

    ESP_LOGI(TAG, "INA3221 Initialized");
    return true;
}

bool Ina3221::get_data(PowerMonitorData &data)
{
    // Channel 1: HV Power
    int16_t shunt_mv = get_shunt_voltage(1);
    int16_t bus_mv = get_bus_voltage(1);
    data.hv.voltage_mv = bus_mv;
    data.hv.current_ma = static_cast<int16_t>(shunt_mv / shunt_resistor_mv_ma_[0]);
    data.hv.power_mw = (static_cast<int32_t>(data.hv.voltage_mv) * data.hv.current_ma) / 1000;

    // Channel 2: 5V Charging Power
    shunt_mv = get_shunt_voltage(2);
    bus_mv = get_bus_voltage(2);
    data.charging.voltage_mv = bus_mv;
    data.charging.current_ma = static_cast<int16_t>(shunt_mv / shunt_resistor_mv_ma_[1]);
    data.charging.power_mw = (static_cast<int32_t>(data.charging.voltage_mv) * data.charging.current_ma) / 1000;

    // Channel 3: LED Backlight Power
    shunt_mv = get_shunt_voltage(3);
    bus_mv = get_bus_voltage(3);
    data.led.voltage_mv = bus_mv;
    data.led.current_ma = static_cast<int16_t>(shunt_mv / shunt_resistor_mv_ma_[2]);
    data.led.power_mw = (static_cast<int32_t>(data.led.voltage_mv) * data.led.current_ma) / 1000;

    return true;
}

int16_t Ina3221::get_shunt_voltage(uint8_t channel)
{
    uint8_t reg = 0;
    switch (channel) {
        case 1: reg = kRegShuntVoltage1; break;
        case 2: reg = kRegShuntVoltage2; break;
        case 3: reg = kRegShuntVoltage3; break;
        default: return 0;
    }

    uint16_t val;
    if (read_register(reg, &val)) {
        // Shunt voltage is in 5uV steps (LSB = 5uV)
        // But register is 16-bit signed.
        // 40uV per LSB? Datasheet says:
        // Shunt Voltage Register: LSB = 40 uV? No wait.
        // Datasheet: Shunt Voltage Register LSB = 5 uV?
        // Let's check datasheet.
        // INA3221: Shunt Voltage LSB = 40 uV.
        // Range +/- 163.8 mV.
        // Wait, 40uV = 0.04mV.
        // So val * 0.04 = mV.
        // Or val * 40 / 1000.
        
        // Wait, let's double check LSB.
        // INA3221 Datasheet: "The LSB for the shunt voltage measurements is 40 uV."
        // 12-bit + sign? No, 13-bit (12 magnitude + sign).
        // The lower 3 bits are 0.
        // So we shift right by 3 first?
        // "The Shunt Voltage register bits are shifted to the left by 3 bits."
        // So raw value >> 3 is the 12-bit + sign value.
        // Then multiply by 40uV.
        
        int16_t raw = static_cast<int16_t>(val);
        raw >>= 3; // Shift out the lower 3 bits
        return static_cast<int16_t>(raw * 0.04f); // Convert to mV (40uV = 0.04mV)
    }
    return 0;
}

int16_t Ina3221::get_bus_voltage(uint8_t channel)
{
    uint8_t reg = 0;
    switch (channel) {
        case 1: reg = kRegBusVoltage1; break;
        case 2: reg = kRegBusVoltage2; break;
        case 3: reg = kRegBusVoltage3; break;
        default: return 0;
    }

    uint16_t val;
    if (read_register(reg, &val)) {
        // Bus Voltage Register: LSB = 8 mV.
        // Lower 3 bits are 0.
        // So raw >> 3 is the value.
        // Then multiply by 8 mV.
        
        int16_t raw = static_cast<int16_t>(val);
        raw >>= 3;
        return raw * 8; // mV
    }
    return 0;
}

bool Ina3221::write_register(uint8_t reg, uint16_t val)
{
    uint8_t data[2];
    data[0] = (val >> 8) & 0xFF; // MSB
    data[1] = val & 0xFF;        // LSB

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret == ESP_OK;
}

bool Ina3221::read_register(uint8_t reg, uint16_t *val)
{
    uint8_t data[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *val = (data[0] << 8) | data[1]; // MSB first
        return true;
    }
    return false;
}