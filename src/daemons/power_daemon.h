#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "powermonitor_driver.h"
#include "message_types.h"

class PowerDaemon
{
public:
    PowerDaemon(IPowerMonitorDriver &driver, QueueHandle_t system_queue);
    ~PowerDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();

    IPowerMonitorDriver &driver_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;
};