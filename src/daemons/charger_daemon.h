#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "charger_driver.h"
#include "message_types.h"

class ChargerDaemon
{
public:
    ChargerDaemon(IChargerDriver &driver, QueueHandle_t system_queue);
    ~ChargerDaemon();

    void start();

    bool read_status_register(uint8_t &status);
    bool read_power_on_config_register(uint8_t &reg01);

    bool enable_charging();
    bool disable_charging();
    bool set_charge_current_ma(uint16_t current_ma);

    bool enable_otg();
    bool disable_otg();
    bool set_otg_voltage_mv(uint16_t voltage_mv);

private:
    static void task_entry(void *param);
    void loop();

    IChargerDriver &driver_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;
};

