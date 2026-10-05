#include "i2c_bus.h"

#include "esp_log.h"

static const char *TAG = "i2c_bus";

struct BusState {
    i2c_master_bus_handle_t handle;
    SemaphoreHandle_t mutex;
    uint32_t scl_hz;
};

static BusState s_bus[I2C_NUM_MAX] = {};

static bool port_ok(i2c_port_t port)
{
    return port >= 0 && port < I2C_NUM_MAX;
}

void i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t scl_hz)
{
    if (!port_ok(port)) {
        ESP_LOGE(TAG, "invalid I2C port %d", static_cast<int>(port));
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    if (s_bus[port].handle) {
        return;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = static_cast<i2c_port_num_t>(port);
    bus_config.sda_io_num = sda;
    bus_config.scl_io_num = scl;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_bus[port].handle));
    s_bus[port].scl_hz = scl_hz;
    if (!s_bus[port].mutex) {
        s_bus[port].mutex = xSemaphoreCreateMutex();
    }
    ESP_LOGI(TAG, "I2C%d SDA=%d SCL=%d %lu Hz",
             static_cast<int>(port), static_cast<int>(sda), static_cast<int>(scl),
             static_cast<unsigned long>(scl_hz));
}

i2c_master_dev_handle_t i2c_bus_add_device(i2c_port_t port, uint8_t address_7bit)
{
    if (!port_ok(port) || !s_bus[port].handle) {
        ESP_LOGE(TAG, "I2C port %d is not initialized", static_cast<int>(port));
        return nullptr;
    }

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = address_7bit;
    dev_config.scl_speed_hz = s_bus[port].scl_hz;

    i2c_master_dev_handle_t handle = nullptr;
    const esp_err_t err = i2c_master_bus_add_device(s_bus[port].handle, &dev_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add 0x%02X on I2C%d failed: %s",
                 address_7bit, static_cast<int>(port), esp_err_to_name(err));
        return nullptr;
    }
    return handle;
}

I2cBusLock::I2cBusLock(i2c_port_t port)
    : mutex_(port_ok(port) ? s_bus[port].mutex : nullptr)
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

esp_err_t i2c_bus_write(i2c_master_dev_handle_t dev, uint8_t reg,
                        const uint8_t *data, size_t len, int timeout_ms)
{
    if (!dev || (len > 0 && !data)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return i2c_master_transmit(dev, &reg, 1, timeout_ms);
    }

    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {&reg, 1},
        {data, len},
    };
    return i2c_master_multi_buffer_transmit(dev, buffers, 2, timeout_ms);
}

esp_err_t i2c_bus_read(i2c_master_dev_handle_t dev, uint8_t reg,
                       uint8_t *data, size_t len, int timeout_ms)
{
    if (!dev || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, timeout_ms);
}
