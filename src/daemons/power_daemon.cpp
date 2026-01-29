#include "daemons/power_daemon.h"
#include "esp_log.h"

static const char *TAG = "PowerDaemon";

PowerDaemon::PowerDaemon(IPowerMonitorDriver &driver, QueueHandle_t system_queue)
    : driver_(driver),
      system_queue_(system_queue),
      task_handle_(nullptr)
{
}

PowerDaemon::~PowerDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void PowerDaemon::start()
{
    xTaskCreate(task_entry, "power_daemon", 4096, this, 5, &task_handle_);
}

void PowerDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<PowerDaemon *>(param);
    daemon->loop();
}

void PowerDaemon::loop()
{
    ESP_LOGI(TAG, "Power Daemon Started");

    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize Power Monitor Driver");
        // Retry logic could be added here
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update every second

    while (true) {
        PowerMonitorData data;
        if (driver_.get_data(data)) {
            // Send data to System Controller
            SystemMessage msg;
            msg.event = SystemEvent::POWER_UPDATE;
            msg.data.power = data;
            
            if (xQueueSend(system_queue_, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "System queue full, dropped power update");
            }
        } else {
            ESP_LOGW(TAG, "Failed to read power data");
        }

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}