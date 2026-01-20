#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "message_types.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"

class SystemController
{
public:
    SystemController(DisplayDaemon &display_daemon, AudioDaemon &audio_daemon);
    ~SystemController();

    void start();
    QueueHandle_t get_queue() const;

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const SystemMessage &msg);
    void update_time();

    DisplayDaemon &display_daemon_;
    AudioDaemon &audio_daemon_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;
    
    // State
    // RTC handle, WiFi handle, etc. can be added here
};