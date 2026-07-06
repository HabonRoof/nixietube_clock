#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "system_controller.h"
#include "charger_controller.h"
#include "power_controller.h"
#include "gasgauge_service.h"
#include "system_state.h"

class AudioDaemon;

class CliDaemon
{
public:
    CliDaemon(SystemController &system_controller,
              ChargerController &charger_controller,
              GasgaugeService &gasgauge_service,
              PowerController &power_controller,
              SystemState &system_state,
              AudioDaemon &audio_daemon);
    ~CliDaemon();

    void start();

private:
    static void task_entry(void *param);
    void loop();
    void register_commands();

    SystemController &system_controller_;
    ChargerController &charger_controller_;
    PowerController &power_controller_;
    GasgaugeService &gasgauge_service_;
    SystemState &system_state_;
    AudioDaemon &audio_daemon_;
    TaskHandle_t task_handle_;
};
