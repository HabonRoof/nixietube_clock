#pragma once

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Install the ESP-IDF v5+ I2C master driver on `port`.
// `scl_hz` is the clock used for every device later added on this bus.
void i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t scl_hz);

// Register a 7-bit device on an already-initialized bus.
// Returns nullptr when the bus is missing or the device cannot be added.
i2c_master_dev_handle_t i2c_bus_add_device(i2c_port_t port, uint8_t address_7bit);

// Serializes multi-step sequences on one port. The new driver already locks a
// single transfer; this mutex keeps a caller's write-then-read (or block
// update) from being interleaved with another device on the same bus.
class I2cBusLock
{
public:
    explicit I2cBusLock(i2c_port_t port);
    ~I2cBusLock();

    I2cBusLock(const I2cBusLock &) = delete;
    I2cBusLock &operator=(const I2cBusLock &) = delete;

private:
    SemaphoreHandle_t mutex_;
};

// Register write: START + addr/W + reg + payload + STOP.
// Does not take I2cBusLock. `timeout_ms` is a millisecond timeout (-1 waits forever).
esp_err_t i2c_bus_write(i2c_master_dev_handle_t dev, uint8_t reg,
                        const uint8_t *data, size_t len, int timeout_ms);

// Register read: START + addr/W + reg + RESTART + addr/R + payload + STOP.
esp_err_t i2c_bus_read(i2c_master_dev_handle_t dev, uint8_t reg,
                       uint8_t *data, size_t len, int timeout_ms);
