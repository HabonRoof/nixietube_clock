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

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000);
    const TickType_t retry_interval = pdMS_TO_TICKS(30000);

    bool init_ok = driver_.is_ready();
    if (!init_ok) {
        init_ok = driver_.init();
    }
    if (!init_ok) {
        ESP_LOGW(TAG, "Gasgauge unavailable; retrying init every 30s");
    } else {
        ESP_LOGI(TAG, "Gasgauge ready");
        last_wake_time = xTaskGetTickCount();
    }

    while (true) {
        if (!init_ok) {
            vTaskDelay(retry_interval);
            init_ok = driver_.init();
            if (init_ok) {
                ESP_LOGI(TAG, "Gasgauge initialized");
                last_wake_time = xTaskGetTickCount();
            }
            continue;
        }

        GasgaugeData data;
        if (driver_.get_data(data)) {
            SystemMessage msg;
            msg.event = SystemEvent::BATTERY_UPDATE;
            msg.data.battery = data;

            if (xQueueSend(system_queue_, &msg, 0) != pdTRUE) {
                ESP_LOGW(TAG, "System queue full, dropped battery update");
            }
        } else {
            ESP_LOGW(TAG, "Gasgauge read failed; will retry init in 30s");
            init_ok = false;
        }

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}