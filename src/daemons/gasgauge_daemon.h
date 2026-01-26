#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "gasgauge_driver.h"
#include "message_types.h"

class GasgaugeDaemon
{
public:
    GasgaugeDaemon(IGasgaugeDriver &driver, QueueHandle_t system_queue);
    ~GasgaugeDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();

    IGasgaugeDriver &driver_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;
};