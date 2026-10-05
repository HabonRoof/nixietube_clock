#include "ltr303.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include <algorithm>

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

constexpr uint8_t kAlsContrActive = 0x01;
constexpr uint8_t kAlsMeasRate100ms2000ms = 0x28;
constexpr uint8_t kStatusDataValid = 0x04;

} // namespace

Ltr303::Ltr303(i2c_port_t port, uint8_t address)
    : port_(port), dev_(i2c_bus_add_device(port, address))
{
}

bool Ltr303::write_als_control()
{
    const uint8_t value =
        kAlsContrActive | (static_cast<uint8_t>(gain_) << 2);
    return write_register(kRegAlsContr, value);
}

bool Ltr303::set_gain(Ltr303Gain gain)
{
    if (!ready_) {
        return false;
    }
    gain_ = gain;
    if (!write_als_control()) {
        ESP_LOGE(TAG, "Failed to set ALS gain");
        return false;
    }
    ESP_LOGI(TAG, "ALS gain set to %s", gain_label(gain_));
    return true;
}

bool Ltr303::is_saturated(const Ltr303Sample &sample)
{
    return sample.ch0 >= kSaturationThreshold || sample.ch1 >= kSaturationThreshold;
}

const char *Ltr303::gain_label(Ltr303Gain gain)
{
    switch (gain) {
        case Ltr303Gain::X1:
            return "1x";
        case Ltr303Gain::X2:
            return "2x";
        case Ltr303Gain::X4:
            return "4x";
        case Ltr303Gain::X8:
            return "8x";
        case Ltr303Gain::X48:
            return "48x";
        case Ltr303Gain::X96:
            return "96x";
        default:
            return "?x";
    }
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

    gain_ = Ltr303Gain::X1;
    if (!write_als_control()) {
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
    ESP_LOGI(TAG, "LTR-303 initialized (gain %s)", gain_label(gain_));
    return true;
}

bool Ltr303::read_channels(Ltr303Sample *sample_out)
{
    if (!sample_out || !ready_) {
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

    const float sum = static_cast<float>(ch0) + static_cast<float>(ch1);
    sample_out->ch0 = ch0;
    sample_out->ch1 = ch1;
    sample_out->ratio = sum > 0.0f ? static_cast<float>(ch1) / sum : 0.0f;
    sample_out->lux = compute_lux(ch0, ch1);
    return true;
}

bool Ltr303::read_raw_lux(float *lux_out)
{
    Ltr303Sample sample{};
    if (!read_channels(&sample)) {
        return false;
    }
    *lux_out = sample.lux;
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
    return i2c_bus_write(dev_, reg, &val, 1, 100) == ESP_OK;
}

bool Ltr303::read_registers(uint8_t reg, uint8_t *data, size_t len)
{
    I2cBusLock lock(port_);
    return i2c_bus_read(dev_, reg, data, len, 100) == ESP_OK;
}
