#pragma once

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void i2c_bus_init(i2c_port_t port);

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
