#include "i2c_bus.h"
#include "debug_session_log.h"

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
    // #region agent log
    if (!mutex_) {
        dbg_session_log("A", "i2c_bus.cpp:I2cBusLock", "mutex_null",
                        static_cast<int32_t>(port), 0, 0);
    }
    // #endregion
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
