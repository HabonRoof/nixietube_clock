#include "daemons/charger_daemon.h"

#include "esp_log.h"

static const char *TAG = "ChargerDaemon";

ChargerDaemon::ChargerDaemon(IChargerDriver &driver, QueueHandle_t system_queue)
    : driver_(driver),
      system_queue_(system_queue),
      task_handle_(nullptr)
{
}

ChargerDaemon::~ChargerDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void ChargerDaemon::start()
{
    xTaskCreate(task_entry, "charger_daemon", 4096, this, 5, &task_handle_);
}


bool ChargerDaemon::read_status_register(uint8_t &status)
{
    return driver_.read_status_register(status);
}

bool ChargerDaemon::read_power_on_config_register(uint8_t &reg01)
{
    return driver_.read_power_on_config_register(reg01);
}

bool ChargerDaemon::enable_charging()
{
    // Keep charging/OTG mutually exclusive.
    if (!driver_.disable_otg()) {
        return false;
    }
    return driver_.enable_charging();
}

bool ChargerDaemon::disable_charging()
{
    return driver_.disable_charging();
}

bool ChargerDaemon::set_charge_current_ma(uint16_t current_ma)
{
    return driver_.set_charge_current_ma(current_ma);
}

bool ChargerDaemon::enable_otg()
{
    // Keep charging/OTG mutually exclusive.
    if (!driver_.disable_charging()) {
        return false;
    }
    return driver_.enable_otg();
}

bool ChargerDaemon::disable_otg()
{
    return driver_.disable_otg();
}

bool ChargerDaemon::set_otg_voltage_mv(uint16_t voltage_mv)
{
    return driver_.set_otg_voltage_mv(voltage_mv);
}

void ChargerDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<ChargerDaemon *>(param);
    daemon->loop();
}

void ChargerDaemon::loop()
{
    ESP_LOGI(TAG, "Charger Daemon Started");

    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize charger driver");
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000);

    while (true) {
        ChargerData data;
        if (driver_.get_data(data)) {
            SystemMessage msg;
            msg.event = SystemEvent::CHARGER_UPDATE;
            msg.data.charger = data;

            if (xQueueSend(system_queue_, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "System queue full, dropped charger update");
            }
        } else {
            ESP_LOGW(TAG, "Failed to read charger status");
        }

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}

