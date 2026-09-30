#include "husb238.h"

#include "i2c_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HUSB238";

static constexpr uint8_t kRegPdStatus0 = 0x00;
static constexpr uint8_t kRegPdStatus1 = 0x01;
static constexpr uint8_t kRegSrcPdo12V = 0x04;
static constexpr uint8_t kRegSrcPdo = 0x08;
static constexpr uint8_t kRegGoCommand = 0x09;

static constexpr uint8_t kGoRequestPdo = 0x01;
static constexpr uint8_t kGoGetSrcCap = 0x04;

static constexpr uint32_t kAttachWaitMs = 500;
static constexpr uint32_t kCapabilityWaitMs = 500;
static constexpr uint32_t kContractWaitMs = 1000;
static constexpr uint32_t kPollMs = 50;

Husb238::Husb238(i2c_port_t port, uint8_t address)
    : port_(port),
      address_(address)
{
}

bool Husb238::read_reg(uint8_t reg, uint8_t *val)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool Husb238::write_reg(uint8_t reg, uint8_t val)
{
    I2cBusLock lock(port_);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (address_ << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(port_, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret == ESP_OK;
}

bool Husb238::read_contract(Husb238Contract &contract)
{
    uint8_t status0 = 0;
    uint8_t status1 = 0;
    uint8_t src_12v = 0;
    if (!read_reg(kRegPdStatus0, &status0) ||
        !read_reg(kRegPdStatus1, &status1) ||
        !read_reg(kRegSrcPdo12V, &src_12v)) {
        contract.i2c_ok = false;
        return false;
    }

    contract.i2c_ok = true;
    contract.attached = (status1 & (1u << 6)) != 0;
    contract.pdo_12v_advertised = (src_12v & (1u << 7)) != 0;
    contract.voltage = static_cast<Husb238Voltage>((status0 >> 4) & 0x0F);
    contract.response = static_cast<Husb238Response>((status1 >> 3) & 0x07);
    contract.current_code = status0 & 0x0F;
    return true;
}

bool Husb238::wait_until(bool (Husb238::*predicate)(Husb238Contract &), Husb238Contract &contract, uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    do {
        if ((this->*predicate)(contract)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    } while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0);
    return (this->*predicate)(contract);
}

bool Husb238::attached(Husb238Contract &contract)
{
    return read_contract(contract) && contract.attached;
}

bool Husb238::twelve_volt_advertised(Husb238Contract &contract)
{
    return read_contract(contract) && contract.pdo_12v_advertised;
}

bool Husb238::twelve_volt_contract(Husb238Contract &contract)
{
    return read_contract(contract) && contract.voltage == Husb238Voltage::V12;
}

Husb238RequestResult Husb238::request_12v(Husb238Contract &contract)
{
    contract = {};
    if (!read_contract(contract)) {
        ESP_LOGE(TAG, "I2C probe failed");
        return Husb238RequestResult::I2cFail;
    }

    if (!wait_until(&Husb238::attached, contract, kAttachWaitMs)) {
        ESP_LOGW(TAG, "USB-C source not attached");
        return Husb238RequestResult::NotAttached;
    }

    if (!contract.pdo_12v_advertised) {
        if (!write_reg(kRegGoCommand, kGoGetSrcCap)) {
            return Husb238RequestResult::I2cFail;
        }
        if (!wait_until(&Husb238::twelve_volt_advertised, contract, kCapabilityWaitMs)) {
            ESP_LOGW(TAG, "Source did not advertise 12V");
            return Husb238RequestResult::NotAdvertised;
        }
    }

    const uint8_t select = static_cast<uint8_t>(static_cast<uint8_t>(Husb238PdoSelect::V12) << 4);
    if (!write_reg(kRegSrcPdo, select) || !write_reg(kRegGoCommand, kGoRequestPdo)) {
        return Husb238RequestResult::I2cFail;
    }

    if (!wait_until(&Husb238::twelve_volt_contract, contract, kContractWaitMs)) {
        if (contract.response == Husb238Response::Invalid ||
            contract.response == Husb238Response::NotSupported ||
            contract.response == Husb238Response::TransactionFail) {
            ESP_LOGW(TAG, "12V request rejected: %s", response_name(contract.response));
            return Husb238RequestResult::Rejected;
        }
        ESP_LOGW(TAG, "12V contract timed out, voltage=%s response=%s",
                 voltage_name(contract.voltage), response_name(contract.response));
        return Husb238RequestResult::Timeout;
    }

    ESP_LOGI(TAG, "12V contract established (%s)", current_name(contract.current_code));
    return Husb238RequestResult::Success;
}

const char *Husb238::voltage_name(Husb238Voltage voltage)
{
    switch (voltage) {
    case Husb238Voltage::V5: return "5V";
    case Husb238Voltage::V9: return "9V";
    case Husb238Voltage::V12: return "12V";
    case Husb238Voltage::V15: return "15V";
    case Husb238Voltage::V18: return "18V";
    case Husb238Voltage::V20: return "20V";
    case Husb238Voltage::Unattached: return "unattached";
    }
    return "unknown";
}

const char *Husb238::current_name(uint8_t current_code)
{
    static const char *kCurrents[] = {
        "0.50A", "0.70A", "1.00A", "1.25A", "1.50A", "1.75A", "2.00A", "2.25A",
        "2.50A", "2.75A", "3.00A", "3.25A", "3.50A", "4.00A", "4.50A", "5.00A",
    };
    if (current_code < (sizeof(kCurrents) / sizeof(kCurrents[0]))) {
        return kCurrents[current_code];
    }
    return "unknown";
}

const char *Husb238::response_name(Husb238Response response)
{
    switch (response) {
    case Husb238Response::NoResponse: return "no response";
    case Husb238Response::Success: return "success";
    case Husb238Response::Invalid: return "invalid command";
    case Husb238Response::NotSupported: return "not supported";
    case Husb238Response::TransactionFail: return "transaction fail";
    }
    return "reserved";
}

const char *Husb238::result_name(Husb238RequestResult result)
{
    switch (result) {
    case Husb238RequestResult::Success: return "success";
    case Husb238RequestResult::I2cFail: return "I2C failed";
    case Husb238RequestResult::NotAttached: return "USB-C not attached";
    case Husb238RequestResult::NotAdvertised: return "12V PDO not advertised";
    case Husb238RequestResult::Rejected: return "request rejected";
    case Husb238RequestResult::Timeout: return "contract timeout";
    }
    return "unknown";
}
