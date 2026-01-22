#include "pca9685.h"
#include "freertos/FreeRTOS.h"

namespace
{
constexpr uint8_t kMode1 = 0x00;
constexpr uint8_t kMode2 = 0x01;
constexpr uint8_t kPrescale = 0xFE;
constexpr uint8_t kLed0OnL = 0x06;
constexpr uint8_t kAllLedOffL = 0xFC;

constexpr uint8_t kMode1Sleep = 0x10;
constexpr uint8_t kMode1Restart = 0x80;
constexpr uint8_t kMode1AutoInc = 0x20;
constexpr uint8_t kMode2TotemPole = 0x04;

constexpr float kOscillatorHz = 25000000.0f;
} // namespace

Pca9685::Pca9685(i2c_port_t port, uint8_t address)
    : port_(port), address_(address)
{
}

bool Pca9685::init_i2c(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t i2c_clock_hz)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda;
    conf.scl_io_num = scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = i2c_clock_hz;

    if (i2c_param_config(port, &conf) != ESP_OK) {
        return false;
    }
    if (i2c_driver_install(port, conf.mode, 0, 0, 0) != ESP_OK) {
        return false;
    }
    return true;
}

bool Pca9685::init(uint32_t i2c_clock_hz, float pwm_frequency_hz)
{
    (void)i2c_clock_hz;
    const uint8_t mode1 = kMode1AutoInc;
    if (!write_register(kMode1, mode1)) {
        return false;
    }
    if (!write_register(kMode2, kMode2TotemPole)) {
        return false;
    }

    float prescale_f = (kOscillatorHz / (4096.0f * pwm_frequency_hz)) - 1.0f;
    if (prescale_f < 3.0f) {
        prescale_f = 3.0f;
    }
    if (prescale_f > 255.0f) {
        prescale_f = 255.0f;
    }
    const uint8_t prescale = static_cast<uint8_t>(prescale_f + 0.5f);

    if (!write_register(kMode1, static_cast<uint8_t>(mode1 | kMode1Sleep))) {
        return false;
    }
    if (!write_register(kPrescale, prescale)) {
        return false;
    }
    if (!write_register(kMode1, mode1)) {
        return false;
    }
    if (!write_register(kMode1, static_cast<uint8_t>(mode1 | kMode1Restart))) {
        return false;
    }
    return true;
}

bool Pca9685::set_pwm(uint8_t channel, uint16_t on, uint16_t off)
{
    if (channel >= 16) {
        return false;
    }
    const uint8_t reg = static_cast<uint8_t>(kLed0OnL + 4 * channel);
    uint8_t data[4] = {
        static_cast<uint8_t>(on & 0xFF),
        static_cast<uint8_t>((on >> 8) & 0x0F),
        static_cast<uint8_t>(off & 0xFF),
        static_cast<uint8_t>((off >> 8) & 0x0F)
    };
    return write_registers(reg, data, sizeof(data));
}

bool Pca9685::set_duty(uint8_t channel, uint16_t duty)
{
    if (duty >= 4095) {
        return set_pwm(channel, 0, 4095);
    }
    return set_pwm(channel, 0, duty);
}

bool Pca9685::set_all_off()
{
    uint8_t data[4] = {0x00, 0x00, 0x00, 0x10};
    return write_registers(kAllLedOffL, data, sizeof(data));
}

bool Pca9685::write_register(uint8_t reg, uint8_t value)
{
    return write_registers(reg, &value, 1);
}

bool Pca9685::write_registers(uint8_t reg, const uint8_t *data, size_t length)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, static_cast<uint8_t>(address_ << 1), true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, const_cast<uint8_t *>(data), length, true);
    i2c_master_stop(cmd);
    esp_err_t result = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return result == ESP_OK;
}
