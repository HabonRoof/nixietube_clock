#include "system_controller.h"
#include "esp_log.h"
#include <ctime>

static const char *TAG = "SystemController";

SystemController::SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon)
    : display_daemon_(display_daemon),
      audio_daemon_(audio_daemon),
      queue_(nullptr),
      task_handle_(nullptr)
{
    queue_ = xQueueCreate(10, sizeof(SystemMessage));
}

SystemController::~SystemController()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (queue_) {
        vQueueDelete(queue_);
    }
}

void SystemController::start()
{
    xTaskCreate(task_entry, "system_controller", 4096, this, 5, &task_handle_);
}

QueueHandle_t SystemController::get_queue() const
{
    return queue_;
}

void SystemController::task_entry(void *param)
{
    auto *controller = static_cast<SystemController *>(param);
    controller->loop();
}

void SystemController::loop()
{
    ESP_LOGI(TAG, "System Controller Started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(1000); // Update time every second

    while (true) {
        SystemMessage msg;
        // Check for system events (buttons, etc.)
        if (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
            process_message(msg);
        }

        // Periodic tasks
        update_time();

        vTaskDelayUntil(&last_wake_time, update_interval);
    }
}

void SystemController::process_message(const SystemMessage &msg)
{
    switch (msg.event) {
        case SystemEvent::BUTTON_PRESSED:
            ESP_LOGI(TAG, "Button pressed: %d", msg.data.button_id);
            // Example: Toggle effect on button press
            // DisplayMessage dmsg;
            // dmsg.command = DisplayCmd::SET_EFFECT;
            // ...
            // xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
            break;
        default:
            break;
    }
}

void SystemController::update_time()
{
    // Get current time (mocked for now, replace with RTC/SNTP)
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Send time update to Display Daemon
    DisplayMessage msg;
    msg.command = DisplayCmd::UPDATE_TIME;
    msg.data.time.h = timeinfo.tm_hour;
    msg.data.time.m = timeinfo.tm_min;
    msg.data.time.s = timeinfo.tm_sec;
    
    xQueueSend(display_daemon_.get_queue(), &msg, 0);
}