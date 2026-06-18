#include "ltr303.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "LTR303";

namespace {

constexpr uint8_t kRegAlsContr = 0x80;
constexpr uint8_t kRegAlsMeasRate = 0x85;
constexpr uint8_t kRegPartId = 0x86;
constexpr uint8_t kRegManufacId = 0x87;
constexpr uint8_t kRegAlsDataCh1_0 = 0x88;
constexpr uint8_t kRegAlsStatus = 0x8C;

constexpr uint8_t kPartId = 0xA0;
constexpr uint8_t kManufacId = 0x05;

// Active mode, ALS gain x1.
constexpr uint8_t kAlsContrActiveGain1x = 0x01;
// 100 ms integration, 2000 ms measurement rate (0.5 Hz).
constexpr uint8_t kAlsMeasRate100ms2000ms = 0x28;

constexpr uint8_t kStatusDataValid = 0x04;

} // namespace

Ltr303::Ltr303(i2c_port_t port, uint8_t address)
    : port_(port), address_(address)
{
}

bool Ltr303::init()
{
    uint8_t part_id = 0;
    uint8_t manufac_id = 0;
    if (!read_register(kRegPartId, &part_id) || !read_register(kRegManufacId, &manufac_id)) {
        ESP_LOGE(TAG, "Failed to read part ID");
        ready_ = false;
        return false;
    }
    if (part_id != kPartId || manufac_id != kManufacId) {
        ESP_LOGE(TAG, "Unexpected ID: part=0x%02x manufac=0x%02x", part_id, manufac_id);
        ready_ = false;
        return false;
    }

    if (!write_register(kRegAlsContr, kAlsContrActiveGain1x)) {
        ESP_LOGE(TAG, "Failed to set ALS control");
        ready_ = false;
        return false;
    }
    if (!write_register(kRegAlsMeasRate, kAlsMeasRate100ms2000ms)) {
        ESP_LOGE(TAG, "Failed to set measurement rate");
        ready_ = false;
        return false;
    }

    ready_ = true;
    ESP_LOGI(TAG, "LTR-303 initialized");
    return true;
}

bool Ltr303::read_raw_lux(float *lux_out)
{
    if (!lux_out || !ready_) {
        return false;
    }

    uint8_t status = 0;
    if (!read_register(kRegAlsStatus, &status)) {
        return false;
    }
    if ((status & kStatusDataValid) == 0) {
        return false;
    }

    uint8_t data[4] = {};
    if (!read_registers(kRegAlsDataCh1_0, data, sizeof(data))) {
        return false;
    }

    const uint16_t ch1 = static_cast<uint16_t>(data[0]) |
                         (static_cast<uint16_t>(data[1]) << 8);
    const uint16_t ch0 = static_cast<uint16_t>(data[2]) |
                         (static_cast<uint16_t>(data[3]) << 8);

    *lux_out = compute_lux(ch0, ch1);
    return true;
}

float Ltr303::compute_lux(uint16_t ch0, uint16_t ch1) const
{
    if (ch0 == 0 && ch1 == 0) {
        return 0.0f;
    }

    const float sum = static_cast<float>(ch0) + static_cast<float>(ch1);
    if (sum <= 0.0f) {
        return 0.0f;
    }

    const float ratio = static_cast<float>(ch1) / sum;
    float lux = 0.0f;

    if (ratio <= 0.45f) {
        lux = 1.7743f * static_cast<float>(ch0) + 1.1059f * static_cast<float>(ch1);
    } else if (ratio <= 0.64f) {
        lux = 4.2785f * static_cast<float>(ch0) - 1.9548f * static_cast<float>(ch1);
    } else if (ratio <= 0.85f) {
        lux = 0.5926f * static_cast<float>(ch0) + 0.2775f * static_cast<float>(ch1);
    }

    if (lux < 0.0f) {
        lux = 0.0f;
    }
    return lux;
}

bool Ltr303::read_register(uint8_t reg, uint8_t *val)
{
    return read_registers(reg, val, 1);
}

bool Ltr303::write_register(uint8_t reg, uint8_t val)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    const esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool Ltr303::read_registers(uint8_t reg, uint8_t *data, size_t len)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    const esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}
