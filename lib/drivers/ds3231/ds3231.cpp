#include "ds3231.h"
#include "esp_log.h"

static const char *TAG = "DS3231";

Ds3231::Ds3231(i2c_port_t port, uint8_t address)
    : port_(port), address_(address)
{
}

bool Ds3231::init()
{
    // Check if device is present
    uint8_t val;
    if (!read_register(0x00, &val)) {
        ESP_LOGE(TAG, "DS3231 not found at address 0x%02x", address_);
        return false;
    }
    return true;
}

bool Ds3231::get_time(struct tm *timeinfo)
{
    uint8_t data[7];
    if (!read_registers(0x00, data, 7)) {
        return false;
    }

    timeinfo->tm_sec = bcd2dec(data[0]);
    timeinfo->tm_min = bcd2dec(data[1]);
    timeinfo->tm_hour = bcd2dec(data[2] & 0x3F); // 24-hour mode assumed
    timeinfo->tm_wday = data[3] - 1; // 1-7 -> 0-6
    timeinfo->tm_mday = bcd2dec(data[4]);
    timeinfo->tm_mon = bcd2dec(data[5] & 0x1F) - 1; // 1-12 -> 0-11
    timeinfo->tm_year = bcd2dec(data[6]) + 100; // 00-99 -> 2000-2099 (tm_year is years since 1900)

    return true;
}

bool Ds3231::set_time(const struct tm *timeinfo)
{
    uint8_t data[7];
    data[0] = dec2bcd(timeinfo->tm_sec);
    data[1] = dec2bcd(timeinfo->tm_min);
    data[2] = dec2bcd(timeinfo->tm_hour);
    data[3] = timeinfo->tm_wday + 1;
    data[4] = dec2bcd(timeinfo->tm_mday);
    data[5] = dec2bcd(timeinfo->tm_mon + 1);
    data[6] = dec2bcd(timeinfo->tm_year % 100);

    return write_registers(0x00, data, 7);
}

bool Ds3231::get_temperature(float *temp)
{
    uint8_t data[2];
    if (!read_registers(0x11, data, 2)) {
        return false;
    }
    int16_t temp_raw = (data[0] << 8) | data[1];
    *temp = temp_raw / 256.0f;
    return true;
}

bool Ds3231::set_alarm1(const struct tm *timeinfo)
{
    // Set Alarm 1 to match seconds, minutes, hours, and day/date
    // A1M1=0, A1M2=0, A1M3=0, A1M4=0 (Match second, minute, hour, and day/date)
    uint8_t data[4];
    data[0] = dec2bcd(timeinfo->tm_sec);
    data[1] = dec2bcd(timeinfo->tm_min);
    data[2] = dec2bcd(timeinfo->tm_hour);
    data[3] = dec2bcd(timeinfo->tm_mday); // Match date (day of month)

    return write_registers(0x07, data, 4);
}

bool Ds3231::clear_alarm1_flag()
{
    uint8_t status;
    if (!read_register(0x0F, &status)) {
        return false;
    }
    status &= ~0x01; // Clear A1F
    return write_register(0x0F, status);
}

bool Ds3231::enable_alarm1_interrupt(bool enable)
{
    uint8_t control;
    if (!read_register(0x0E, &control)) {
        return false;
    }
    if (enable) {
        control |= 0x05; // INTCN=1, A1IE=1
    } else {
        control &= ~0x01; // A1IE=0
    }
    return write_register(0x0E, control);
}

uint8_t Ds3231::bcd2dec(uint8_t val)
{
    return ((val / 16 * 10) + (val % 16));
}

uint8_t Ds3231::dec2bcd(uint8_t val)
{
    return ((val / 10 * 16) + (val % 10));
}

bool Ds3231::read_register(uint8_t reg, uint8_t *val)
{
    return read_registers(reg, val, 1);
}

bool Ds3231::write_register(uint8_t reg, uint8_t val)
{
    return write_registers(reg, &val, 1);
}

bool Ds3231::read_registers(uint8_t reg, uint8_t *data, size_t len)
{
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
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool Ds3231::write_registers(uint8_t reg, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, const_cast<uint8_t*>(data), len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}