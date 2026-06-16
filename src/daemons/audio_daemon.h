#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "message_types.h"
#include "audio_driver.h"

class PowerController;

class AudioDaemon
{
public:
    AudioDaemon(IAudioDriver &driver, PowerController &power_controller);
    ~AudioDaemon();

    void start();
    QueueHandle_t get_queue() const;

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const AudioMessage &msg);
    void ensure_dfplayer_power();

    IAudioDriver &driver_;
    PowerController &power_controller_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;
    bool dfplayer_powered_;
};
