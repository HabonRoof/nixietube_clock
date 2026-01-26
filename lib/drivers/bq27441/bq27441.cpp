#include "bq27441.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BQ27441";

// Register Definitions
static constexpr uint8_t kRegControl = 0x00;
static constexpr uint8_t kRegVoltage = 0x04;
static constexpr uint8_t kRegFlags = 0x06;
static constexpr uint8_t kRegAvgCurrent = 0x10;
static constexpr uint8_t kRegSoc = 0x1C;
static constexpr uint8_t kRegSoh = 0x20; // StateOfHealth

// Extended Data Commands
static constexpr uint8_t kRegBlockDataControl = 0x61;
static constexpr uint8_t kRegDataClass = 0x3E;
static constexpr uint8_t kRegDataBlock = 0x3F;
static constexpr uint8_t kRegBlockData = 0x40;
static constexpr uint8_t kRegBlockDataChecksum = 0x60;

// Control Subcommands
static constexpr uint16_t kSubCmdControlStatus = 0x0000;
static constexpr uint16_t kSubCmdDeviceType = 0x0001;
static constexpr uint16_t kSubCmdFwVersion = 0x0002;
static constexpr uint16_t kSubCmdSetCfgupdate = 0x0013;
static constexpr uint16_t kSubCmdSoftReset = 0x0042;
static constexpr uint16_t kSubCmdSealed = 0x0020;

// Configuration
static constexpr uint16_t kDesignCapacityMah = 4200;

Bq27441::Bq27441(i2c_port_t port)
    : port_(port)
{
}

bool Bq27441::init()
{
    // Check if device is present by reading Device Type
    if (!control_command(kSubCmdDeviceType)) {
        ESP_LOGE(TAG, "Failed to communicate with BQ27441");
        return false;
    }
    
    uint16_t result;
    if (!read_word(kRegControl, &result)) {
        return false;
    }
    ESP_LOGI(TAG, "BQ27441 Device Type: 0x%04X", result);

    // Check current Design Capacity
    uint16_t current_capacity;
    if (get_design_capacity(&current_capacity)) {
        ESP_LOGI(TAG, "Current Design Capacity: %d mAh", current_capacity);
        if (current_capacity == kDesignCapacityMah) {
            ESP_LOGI(TAG, "Battery already configured. Skipping configuration.");
            return true;
        }
    } else {
        ESP_LOGW(TAG, "Failed to read current Design Capacity");
    }

    // Configure Battery
    ESP_LOGI(TAG, "Configuring battery to %d mAh...", kDesignCapacityMah);
    if (!configure_battery(kDesignCapacityMah)) {
        ESP_LOGE(TAG, "Failed to configure battery");
        return false;
    }

    return true;
}

bool Bq27441::get_data(GasgaugeData &data)
{
    uint16_t val;

    // Voltage (mV)
    if (!read_word(kRegVoltage, &val)) return false;
    data.voltage_mv = val;

    // Average Current (mA)
    if (!read_word(kRegAvgCurrent, &val)) return false;
    data.current_ma = static_cast<int16_t>(val);

    // State of Charge (%)
    if (!read_word(kRegSoc, &val)) return false;
    data.soc = static_cast<uint8_t>(val & 0xFF);

    return true;
}

bool Bq27441::configure_battery(uint16_t capacity_mah)
{
    // 1. Unseal
    if (!unseal()) return false;

    // 2. Enter Config Mode
    if (!enter_config_mode()) return false;

    // 3. Update Design Capacity
    if (!set_design_capacity(capacity_mah)) return false;

    // 4. Exit Config Mode
    if (!exit_config_mode()) return false;

    // 5. Seal
    if (!seal()) return false;

    return true;
}

bool Bq27441::unseal()
{
    // Write 0x8000 to Control() twice to unseal
    if (!control_command(0x8000)) return false;
    if (!control_command(0x8000)) return false;
    return true;
}

bool Bq27441::seal()
{
    return control_command(kSubCmdSealed);
}

bool Bq27441::enter_config_mode()
{
    if (!control_command(kSubCmdSetCfgupdate)) return false;

    // Wait for CFGUPMODE bit in Flags
    int timeout = 100;
    while (timeout--) {
        uint16_t flags;
        if (read_word(kRegFlags, &flags)) {
            if (flags & 0x0010) { // CFGUPMODE bit 4
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "Timeout entering config mode");
    return false;
}

bool Bq27441::exit_config_mode()
{
    if (!control_command(kSubCmdSoftReset)) return false;

    // Wait for CFGUPMODE bit to clear
    int timeout = 100;
    while (timeout--) {
        uint16_t flags;
        if (read_word(kRegFlags, &flags)) {
            if (!(flags & 0x0010)) {
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "Timeout exiting config mode");
    return false;
}

bool Bq27441::set_design_capacity(uint16_t capacity_mah)
{
    // Design Capacity is in State subclass (82), offset 10
    // Data Class: 82 (0x52)
    // Offset: 10 (0x0A)
    // Type: I2 (2 bytes)
    
    uint8_t data[2];
    // Big Endian for Block Data?
    // BQ27441 uses Big Endian for Block Data Memory
    data[0] = (capacity_mah >> 8) & 0xFF;
    data[1] = capacity_mah & 0xFF;

    return write_block_data(82, 10, data, 2);
}

bool Bq27441::get_design_capacity(uint16_t *capacity_mah)
{
    // Design Capacity is in State subclass (82), offset 10
    // Data Class: 82 (0x52)
    // Offset: 10 (0x0A)
    
    // Must be unsealed to access BlockDataControl
    if (!unseal()) return false;

    // 1. Enable Block Data Memory Control
    if (!write_word(kRegBlockDataControl, 0x00)) {
        return false;
    }

    // 2. Write Data Class
    if (!write_word(kRegDataClass, 82)) {
        return false;
    }

    // 3. Write Data Block
    // Offset 10 is in block 0
    if (!write_word(kRegDataBlock, 0)) {
        return false;
    }

    // 4. Read Block Data
    uint8_t block_buffer[32];
    if (!read_block(kRegBlockData, block_buffer, 32)) {
        return false;
    }

    // 5. Extract Capacity (Offset 10, 2 bytes, Big Endian)
    *capacity_mah = (block_buffer[10] << 8) | block_buffer[11];
    
    // Re-seal
    seal();
    
    return true;
}

bool Bq27441::write_block_data(uint8_t class_id, uint8_t offset, uint8_t *data, uint8_t len)
{
    // 1. Enable Block Data Memory Control
    if (!write_word(kRegBlockDataControl, 0x00)) return false;

    // 2. Write Data Class
    // The BlockData() command (0x40...0x5F) is used to access the data.
    // We need to set the DataClass() (0x3E) and DataBlock() (0x3F).
    // DataBlock is the 32-byte block index. Offset 10 is in block 0 (0-31).
    // If offset > 31, we need to adjust block and offset.
    
    uint8_t block_index = offset / 32;
    uint8_t block_offset = offset % 32;
    
    if (!write_word(kRegDataClass, class_id)) return false;
    if (!write_word(kRegDataBlock, block_index)) return false;

    // 3. Read the block to update checksum later?
    // Actually, we can just write the bytes we want, but we need to calculate checksum for the whole block?
    // No, we can write specific bytes, but we must update the checksum.
    // The checksum is the complement of the sum of all bytes in the block (0x40-0x5F).
    // So we MUST read the whole block first, modify it, then write it back and update checksum.
    
    uint8_t block_buffer[32];
    if (!read_block(kRegBlockData, block_buffer, 32)) return false;

    // Modify data
    for (int i = 0; i < len; ++i) {
        block_buffer[block_offset + i] = data[i];
    }

    // 4. Write modified data
    // We can just write the modified bytes to the specific registers?
    // Yes, but let's write the whole block or just the part?
    // Writing to 0x40 + block_offset
    
    // Let's write just the modified bytes
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, kRegBlockData + block_offset, true);
    i2c_master_write(cmd, data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) return false;

    // 5. Update Checksum
    // Calculate new checksum
    uint8_t new_checksum = 0;
    for (int i = 0; i < 32; ++i) {
        new_checksum += block_buffer[i];
    }
    new_checksum = 0xFF - new_checksum;

    if (!write_word(kRegBlockDataChecksum, new_checksum)) return false;

    return true;
}

bool Bq27441::control_command(uint16_t subcommand)
{
    return write_word(kRegControl, subcommand);
}

bool Bq27441::read_word(uint8_t reg, uint16_t *val)
{
    uint8_t data[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        *val = (data[1] << 8) | data[0]; // Little Endian
        return true;
    }
    return false;
}

bool Bq27441::write_word(uint8_t reg, uint16_t val)
{
    uint8_t data[2];
    data[0] = val & 0xFF;
    data[1] = (val >> 8) & 0xFF;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret == ESP_OK;
}

bool Bq27441::read_block(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (kAddress << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret == ESP_OK;
}