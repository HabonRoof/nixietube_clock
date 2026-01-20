#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "message_types.h"
#include "audio_driver.h"

class AudioDaemon
{
public:
    explicit AudioDaemon(IAudioDriver &driver);
    ~AudioDaemon();

    void start();
    QueueHandle_t get_queue() const;

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const AudioMessage &msg);

    IAudioDriver &driver_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;
};