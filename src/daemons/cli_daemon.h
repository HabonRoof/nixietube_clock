#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "system_controller.h"

class CliDaemon
{
public:
    CliDaemon(SystemController &system_controller);
    ~CliDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();
    void register_commands();

    SystemController &system_controller_;
    TaskHandle_t task_handle_;
};