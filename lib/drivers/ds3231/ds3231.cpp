#include "ds3231.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "DS3231";

Ds3231::Ds3231(i2c_port_t port, uint8_t address)
    : port_(port),
      dev_(i2c_bus_add_device(port, address)),
      address_(address)
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

    uint8_t hour_reg = data[2];
    if (hour_reg & 0x40) {
        // 12-hour mode: bits 0-4 hold the hour (1-12), bit5 is PM.
        uint8_t hour12 = bcd2dec(hour_reg & 0x1F);
        bool pm = (hour_reg & 0x20) != 0;
        if (hour12 == 12) {
            hour12 = 0; // 12 AM -> 0, 12 PM -> 12 (handled by pm below)
        }
        timeinfo->tm_hour = pm ? hour12 + 12 : hour12;
    } else {
        // 24-hour mode.
        timeinfo->tm_hour = bcd2dec(hour_reg & 0x3F);
    }

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
    data[2] = dec2bcd(timeinfo->tm_hour); // bit6 = 0 forces 24-hour mode
    data[3] = timeinfo->tm_wday + 1;
    data[4] = dec2bcd(timeinfo->tm_mday);
    data[5] = dec2bcd(timeinfo->tm_mon + 1);
    data[6] = dec2bcd(timeinfo->tm_year % 100);

    if (!write_registers(0x00, data, 7)) {
        return false;
    }

    // Time is now valid; clear the oscillator stop flag.
    clear_osf();
    return true;
}

bool Ds3231::oscillator_stopped(bool *stopped)
{
    if (!stopped) {
        return false;
    }
    uint8_t status;
    if (!read_register(0x0F, &status)) {
        return false;
    }
    *stopped = (status & 0x80) != 0;
    return true;
}

bool Ds3231::clear_osf()
{
    uint8_t status;
    if (!read_register(0x0F, &status)) {
        return false;
    }
    status &= ~0x80; // Clear OSF
    return write_register(0x0F, status);
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
    // Daily alarm: match second/minute/hour; ignore day/date (A1M4=1).
    uint8_t data[4];
    data[0] = dec2bcd(timeinfo->tm_sec);
    data[1] = dec2bcd(timeinfo->tm_min);
    data[2] = dec2bcd(timeinfo->tm_hour);
    data[3] = 0x80; // A1M4=1 -> repeat every day

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

bool Ds3231::alarm1_triggered(bool *triggered)
{
    if (!triggered) {
        return false;
    }
    uint8_t status;
    if (!read_register(0x0F, &status)) {
        return false;
    }
    *triggered = (status & 0x01) != 0;
    return true;
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
    I2cBusLock lock(port_);
    return i2c_bus_read(dev_, reg, data, len, 1000) == ESP_OK;
}

bool Ds3231::write_registers(uint8_t reg, const uint8_t *data, size_t len)
{
    I2cBusLock lock(port_);
    return i2c_bus_write(dev_, reg, data, len, 1000) == ESP_OK;
}