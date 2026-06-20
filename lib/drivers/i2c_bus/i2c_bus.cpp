#include "i2c_bus.h"

static SemaphoreHandle_t s_mutex[I2C_NUM_MAX] = {};

void i2c_bus_init(i2c_port_t port)
{
    if (port >= I2C_NUM_MAX) {
        return;
    }
    if (!s_mutex[port]) {
        s_mutex[port] = xSemaphoreCreateMutex();
    }
}

I2cBusLock::I2cBusLock(i2c_port_t port)
    : mutex_(port < I2C_NUM_MAX ? s_mutex[port] : nullptr)
{
    if (mutex_) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

I2cBusLock::~I2cBusLock()
{
    if (mutex_) {
        xSemaphoreGive(mutex_);
    }
}
