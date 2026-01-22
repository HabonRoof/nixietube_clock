#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/uart.h"
#include "message_types.h"
#include "daemons/display_daemon.h"
#include "daemons/audio_daemon.h"
#include "ds3231/ds3231.h"

struct HardwareHandles {
    i2c_port_t i2c_port;
    rmt_channel_handle_t led_rmt_channel;
    rmt_encoder_handle_t led_rmt_encoder;
    uart_port_t audio_uart_port;
};

class SystemController
{
public:
    static HardwareHandles init_hardware();

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
    Ds3231 rtc_;
};