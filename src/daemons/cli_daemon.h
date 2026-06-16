#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "system_controller.h"
#include "charger_controller.h"
#include "power_controller.h"
#include "daemons/gasgauge_daemon.h"

class CliDaemon
{
public:
    CliDaemon(SystemController &system_controller,
              ChargerController &charger_controller,
              GasgaugeDaemon &gasgauge_daemon,
              PowerController &power_controller);
    ~CliDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();
    void register_commands();

    SystemController &system_controller_;
    ChargerController &charger_controller_;
    PowerController &power_controller_;
    GasgaugeDaemon &gasgauge_daemon_;
    TaskHandle_t task_handle_;
};
