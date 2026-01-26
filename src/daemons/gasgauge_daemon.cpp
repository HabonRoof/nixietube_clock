#include "daemons/gasgauge_daemon.h"
#include "esp_log.h"

static const char *TAG = "GasgaugeDaemon";

GasgaugeDaemon::GasgaugeDaemon(IGasgaugeDriver &driver, QueueHandle_t system_queue)
    : driver_(driver),
      system_queue_(system_queue),
      task_handle_(nullptr)
{
}

GasgaugeDaemon::~GasgaugeDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void GasgaugeDaemon::start()
{
    xTaskCreate(task_entry, "gasgauge_daemon", 4096, this, 5, &task_handle_);
}

void GasgaugeDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<GasgaugeDaemon *>(param);
    daemon->loop();
}

void GasgaugeDaemon::loop()
{
    ESP_LOGI(TAG, "Gasgauge Daemon Started");

    if (!driver_.init()) {
        ESP_LOGE(TAG, "Failed to initialize Gasgauge Driver");
        // Should we retry or just exit?
        // Let's retry periodically
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update every second

    while (true) {
        GasgaugeData data;
        if (driver_.get_data(data)) {
            // Send data to System Controller
            SystemMessage msg;
            msg.event = SystemEvent::BATTERY_UPDATE;
            msg.data.battery = data;
            
            // Use a timeout of 0 to avoid blocking if queue is full
            if (xQueueSend(system_queue_, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "System queue full, dropped battery update");
            }
        } else {
            ESP_LOGW(TAG, "Failed to read gasgauge data");
            // Try to re-init if communication fails repeatedly?
        }

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}