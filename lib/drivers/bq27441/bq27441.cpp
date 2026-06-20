#include "bq27441.h"
#include "bq27441_regs.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BQ27441";

using namespace bq27441;

namespace {

constexpr int kI2cRetries = 3;
thread_local esp_err_t g_last_i2c_err = ESP_OK;

template <typename Fn>
esp_err_t i2c_retry(Fn fn)
{
    esp_err_t last = ESP_FAIL;
    for (int attempt = 0; attempt < kI2cRetries; ++attempt) {
        last = fn();
        g_last_i2c_err = last;
        if (last == ESP_OK) {
            return ESP_OK;
        }
        if (attempt + 1 < kI2cRetries) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return last;
}

} // namespace

Bq27441::Bq27441(i2c_port_t port)
    : port_(port)
{
}

bool Bq27441::is_ready() const
{
    return ready_;
}

uint8_t Bq27441::checksum_replace(uint8_t old_csum,
                                  const uint8_t *old_bytes,
                                  const uint8_t *new_bytes,
                                  size_t len)
{
    uint16_t temp = static_cast<uint16_t>(255U - old_csum);
    for (size_t i = 0; i < len; ++i) {
        temp = (temp + 256U - old_bytes[i]) & 0xFFU;
    }
    for (size_t i = 0; i < len; ++i) {
        temp = (temp + new_bytes[i]) & 0xFFU;
    }
    return static_cast<uint8_t>(255U - temp);
}

esp_err_t Bq27441::i2c_read_byte(uint8_t reg, uint8_t *val)
{
    if (!val) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_retry([&]() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_READ, true);
        i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    });
}

esp_err_t Bq27441::i2c_write_byte(uint8_t reg, uint8_t val)
{
    return i2c_retry([&]() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_write_byte(cmd, val, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    });
}

esp_err_t Bq27441::i2c_read_word(uint8_t reg, uint16_t *val)
{
    if (!val) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[2];
    esp_err_t ret = i2c_retry([&]() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return err;
    });

    if (ret == ESP_OK) {
        *val = static_cast<uint16_t>((data[1] << 8) | data[0]);
    }
    return ret;
}

esp_err_t Bq27441::i2c_write_word(uint8_t reg, uint16_t val)
{
    uint8_t data[2] = {
        static_cast<uint8_t>(val & 0xFF),
        static_cast<uint8_t>((val >> 8) & 0xFF),
    };

    return i2c_retry([&]() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_write(cmd, data, 2, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    });
}

esp_err_t Bq27441::i2c_read_bytes(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_retry([&]() {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (kI2cAddress << 1) | I2C_MASTER_READ, true);
        if (len > 1) {
            i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        return ret;
    });
}

bool Bq27441::control_command(uint16_t subcommand)
{
    I2cBusLock lock(port_);
    return i2c_write_word(kRegControl, subcommand) == ESP_OK;
}

bool Bq27441::control_subcommand_read(uint16_t subcommand, uint16_t *result)
{
    if (!result) {
        return false;
    }

    I2cBusLock lock(port_);
    if (i2c_write_word(kRegControl, subcommand) != ESP_OK) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
    return i2c_read_word(kRegControl, result) == ESP_OK;
}

bool Bq27441::read_control_status(uint16_t *status)
{
    return control_subcommand_read(kSubCmdControlStatus, status);
}

bool Bq27441::read_flags(uint16_t *flags)
{
    if (!flags) {
        return false;
    }

    I2cBusLock lock(port_);
    return i2c_read_word(kRegFlags, flags) == ESP_OK;
}

bool Bq27441::read_design_capacity(uint16_t *capacity_mah)
{
    if (!capacity_mah) {
        return false;
    }

    I2cBusLock lock(port_);
    return i2c_read_word(kRegDesignCapacity, capacity_mah) == ESP_OK;
}

bool Bq27441::is_sealed()
{
    uint16_t status = 0;
    if (!read_control_status(&status)) {
        return true;
    }
    return (status & kStatusSealed) != 0;
}

bool Bq27441::unseal()
{
    if (!is_sealed()) {
        return true;
    }

    if (!control_command(kUnsealKey)) {
        ESP_LOGE(TAG, "unseal: first key failed");
        return false;
    }
    if (!control_command(kUnsealKey)) {
        ESP_LOGE(TAG, "unseal: second key failed");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(5));

    if (is_sealed()) {
        ESP_LOGE(TAG, "unseal: device still sealed");
        return false;
    }

    return true;
}

bool Bq27441::seal()
{
    if (!control_command(kSubCmdSealed)) {
        ESP_LOGE(TAG, "seal: command failed");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
    return is_sealed();
}

bool Bq27441::enter_config_mode()
{
    if (!control_command(kSubCmdSetCfgupdate)) {
        ESP_LOGE(TAG, "enter_config_mode: SET_CFGUPDATE failed");
        return false;
    }

    for (int timeout = 100; timeout > 0; --timeout) {
        uint16_t flags = 0;
        if (read_flags(&flags) && (flags & kFlagCfgUpdate)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGE(TAG, "enter_config_mode: timeout");
    return false;
}

bool Bq27441::exit_config_mode()
{
    if (!control_command(kSubCmdSoftReset)) {
        ESP_LOGE(TAG, "exit_config_mode: SOFT_RESET failed");
        return false;
    }

    for (int timeout = 100; timeout > 0; --timeout) {
        uint16_t flags = 0;
        if (read_flags(&flags) && !(flags & kFlagCfgUpdate)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGE(TAG, "exit_config_mode: timeout");
    return false;
}

bool Bq27441::select_block(uint8_t class_id, uint8_t block_index)
{
    I2cBusLock lock(port_);
    if (i2c_write_byte(kRegBlockDataControl, 0x00) != ESP_OK) {
        ESP_LOGE(TAG, "select_block: BlockDataControl failed");
        return false;
    }
    if (i2c_write_byte(kRegDataClass, class_id) != ESP_OK) {
        ESP_LOGE(TAG, "select_block: DataClass failed");
        return false;
    }
    if (i2c_write_byte(kRegDataBlock, block_index) != ESP_OK) {
        ESP_LOGE(TAG, "select_block: DataBlock failed");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2));
    return true;
}

bool Bq27441::read_block_buffer(uint8_t out[32], uint8_t *checksum)
{
    if (!out || !checksum) {
        return false;
    }

    I2cBusLock lock(port_);
    if (i2c_read_bytes(kRegBlockData, out, kStateBlockSize) != ESP_OK) {
        ESP_LOGE(TAG, "read_block_buffer: block read failed");
        return false;
    }
    if (i2c_read_byte(kRegBlockDataChecksum, checksum) != ESP_OK) {
        ESP_LOGE(TAG, "read_block_buffer: checksum read failed");
        return false;
    }

    return true;
}

bool Bq27441::write_design_capacity_mah(uint16_t old_capacity_mah, uint16_t new_capacity_mah)
{
    I2cBusLock lock(port_);

    if (i2c_write_byte(kRegBlockDataControl, 0x00) != ESP_OK ||
        i2c_write_byte(kRegDataClass, kStateClassId) != ESP_OK ||
        i2c_write_byte(kRegDataBlock, 0) != ESP_OK) {
        ESP_LOGE(TAG, "write_design_capacity: select block failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t block[kStateBlockSize];
    uint8_t old_csum = 0;
    if (i2c_read_bytes(kRegBlockData, block, kStateBlockSize) != ESP_OK) {
        ESP_LOGE(TAG, "write_design_capacity: block read failed");
        return false;
    }
    if (i2c_read_byte(kRegBlockDataChecksum, &old_csum) != ESP_OK) {
        ESP_LOGE(TAG, "write_design_capacity: checksum read failed");
        return false;
    }

    const uint8_t old_bytes[4] = {
        block[kDesignCapacityOffset],
        block[kDesignCapacityOffset + 1],
        block[kDesignEnergyOffset],
        block[kDesignEnergyOffset + 1],
    };

    const uint16_t old_energy_mwh =
        static_cast<uint16_t>((old_bytes[2] << 8) | old_bytes[3]);
    uint16_t new_energy_mwh = old_energy_mwh;
    if (old_capacity_mah > 0) {
        new_energy_mwh = static_cast<uint16_t>(
            (static_cast<uint32_t>(old_energy_mwh) * new_capacity_mah) / old_capacity_mah);
    }

    const uint8_t new_bytes[4] = {
        static_cast<uint8_t>((new_capacity_mah >> 8) & 0xFF),
        static_cast<uint8_t>(new_capacity_mah & 0xFF),
        static_cast<uint8_t>((new_energy_mwh >> 8) & 0xFF),
        static_cast<uint8_t>(new_energy_mwh & 0xFF),
    };

    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t reg = static_cast<uint8_t>(kRegBlockData + kDesignCapacityOffset + i);
        if (i2c_write_byte(reg, new_bytes[i]) != ESP_OK) {
            ESP_LOGE(TAG, "write_design_capacity: byte write failed at offset %u",
                     kDesignCapacityOffset + i);
            return false;
        }
    }

    const uint8_t new_csum = checksum_replace(old_csum, old_bytes, new_bytes, 4);
    if (i2c_write_byte(kRegBlockDataChecksum, new_csum) != ESP_OK) {
        ESP_LOGE(TAG, "write_design_capacity: checksum write failed");
        return false;
    }

    ESP_LOGI(TAG, "State block update: cap %u->%u mAh, energy %u->%u mWh, csum 0x%02X->0x%02X",
             old_capacity_mah, new_capacity_mah, old_energy_mwh, new_energy_mwh,
             old_csum, new_csum);
    return true;
}

bool Bq27441::probe(GasgaugeDeviceInfo &info)
{
    info = {};

    if (!control_subcommand_read(kSubCmdDeviceType, &info.device_type)) {
        ESP_LOGE(TAG, "probe: device type read failed");
        ready_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Device Type: 0x%04X", info.device_type);

    if (!control_subcommand_read(kSubCmdFwVersion, &info.fw_version)) {
        ESP_LOGE(TAG, "probe: firmware version read failed");
        ready_ = false;
        return false;
    }
    ESP_LOGI(TAG, "FW Version: 0x%04X", info.fw_version);

    if (!read_design_capacity(&info.design_capacity)) {
        ESP_LOGW(TAG, "probe: design capacity read failed");
        info.design_capacity = 0;
    } else {
        ESP_LOGI(TAG, "Design Capacity: %u mAh", info.design_capacity);
    }

    if (read_control_status(&info.control_status)) {
        info.sealed = (info.control_status & kStatusSealed) != 0;
        ESP_LOGI(TAG, "Control Status: 0x%04X (sealed=%s)",
                 info.control_status, info.sealed ? "yes" : "no");
    }

    if (read_flags(&info.flags)) {
        ESP_LOGI(TAG, "Flags: 0x%04X", info.flags);
    }

    info.battery_detected = (info.flags & kFlagBatDetect) != 0;
    info.init_complete = (info.control_status & kStatusInitComp) != 0;
    info.needs_reconfig = (info.flags & kFlagITPOR) != 0;
    const bool cfg_update = (info.flags & kFlagCfgUpdate) != 0;
    const bool sleep = (info.control_status & kStatusSleep) != 0;

    ESP_LOGI(TAG,
             "Gauge state: BAT_DET=%s INITCOMP=%s ITPOR=%s CFGUPMODE=%s SLEEP=%s",
             info.battery_detected ? "yes" : "no",
             info.init_complete ? "yes" : "no",
             info.needs_reconfig ? "yes" : "no",
             cfg_update ? "yes" : "no",
             sleep ? "yes" : "no");

    if (!info.battery_detected) {
        ESP_LOGW(TAG,
                 "No battery detected; gauge remains in INITIALIZATION (predictions invalid)");
    }
    if (!info.init_complete) {
        ESP_LOGW(TAG, "Initialization not complete (INITCOMP=0)");
    }
    if (info.needs_reconfig) {
        ESP_LOGW(TAG, "ITPOR set; configuration reload may be required");
    }

    info.probed = true;

    const bool gauging_ready =
        info.battery_detected &&
        info.init_complete &&
        !cfg_update;
    ready_ = gauging_ready;

    if (!gauging_ready) {
        ESP_LOGW(TAG, "Gauging not ready; get_data polling deferred");
    }

    return true;
}

bool Bq27441::configure(uint16_t capacity_mah, bool force)
{
    uint16_t current_capacity = 0;
    if (!read_design_capacity(&current_capacity)) {
        ESP_LOGE(TAG, "configure: failed to read current capacity");
        return false;
    }

    if (!force && current_capacity == capacity_mah) {
        ESP_LOGI(TAG, "Design capacity already %u mAh", capacity_mah);
        return true;
    }

    ESP_LOGI(TAG, "Configuring design capacity %u -> %u mAh", current_capacity, capacity_mah);

    const bool was_sealed = is_sealed();

    if (!unseal()) {
        return false;
    }

    if (!enter_config_mode()) {
        if (was_sealed) {
            seal();
        }
        return false;
    }

    if (!write_design_capacity_mah(current_capacity, capacity_mah)) {
        exit_config_mode();
        if (was_sealed) {
            seal();
        }
        return false;
    }

    if (!exit_config_mode()) {
        if (was_sealed) {
            seal();
        }
        return false;
    }

    if (was_sealed && !seal()) {
        ESP_LOGW(TAG, "configure: failed to re-seal");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t verified = 0;
    if (!read_design_capacity(&verified)) {
        ESP_LOGE(TAG, "configure: verify read failed");
        return false;
    }

    ESP_LOGI(TAG, "Verified Design Capacity: %u mAh (expected %u)", verified, capacity_mah);
    if (verified != capacity_mah) {
        ESP_LOGE(TAG, "configure: verify mismatch");
        return false;
    }
    return true;
}

bool Bq27441::get_data(GasgaugeData &data)
{
    constexpr int kGetDataRetries = 3;

    for (int attempt = 0; attempt < kGetDataRetries; ++attempt) {
        uint16_t val;
        uint16_t flags = 0;

        I2cBusLock lock(port_);

        const esp_err_t v_err = i2c_read_word(kRegVoltage, &val);
        if (v_err == ESP_OK) {
            i2c_read_word(kRegFlags, &flags);
        }

        if (v_err != ESP_OK) {
            if (attempt + 1 < kGetDataRetries) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "get_data: voltage read failed (err=0x%x last=0x%x flags=0x%04X)",
                     v_err, g_last_i2c_err, flags);
            return false;
        }

        data.voltage_mv = val;

        if (i2c_read_word(kRegAvgCurrent, &val) != ESP_OK) {
            if (attempt + 1 < kGetDataRetries) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "get_data: current read failed");
            return false;
        }
        data.current_ma = static_cast<int16_t>(val);

        if (i2c_read_word(kRegSoc, &val) != ESP_OK) {
            if (attempt + 1 < kGetDataRetries) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "get_data: SOC read failed");
            return false;
        }
        data.soc = static_cast<uint8_t>(val & 0xFF);

        if (i2c_read_word(kRegSoh, &val) != ESP_OK) {
            if (attempt + 1 < kGetDataRetries) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGW(TAG, "get_data: SOH read failed");
            return false;
        }
        data.soh = static_cast<uint8_t>(val & 0xFF);

        return true;
    }

    return false;
}

bool Bq27441::peek_registers(uint8_t reg, uint8_t *out, size_t len)
{
    if (!out || len == 0) {
        return false;
    }

    I2cBusLock lock(port_);
    return i2c_read_bytes(reg, out, len) == ESP_OK;
}

bool Bq27441::dump_state_block(uint8_t class_id, uint8_t block_index,
                               uint8_t out[32], uint8_t *checksum)
{
    if (!out || !checksum) {
        return false;
    }

    const bool was_sealed = is_sealed();

    if (!unseal()) {
        return false;
    }

    if (!enter_config_mode()) {
        if (was_sealed) {
            seal();
        }
        return false;
    }

    bool ok = select_block(class_id, block_index) && read_block_buffer(out, checksum);

    if (!exit_config_mode()) {
        ESP_LOGW(TAG, "dump_state_block: exit config failed");
        ok = false;
    }

    if (was_sealed && !seal()) {
        ESP_LOGW(TAG, "dump_state_block: re-seal failed");
    }

    return ok;
}
