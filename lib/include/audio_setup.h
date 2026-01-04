#pragma once

#include "dfplayer_mini.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include <cstdint>

struct AudioConfig
{
    uart_port_t uart_port;
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    int baud_rate;
    uint8_t default_volume;
    uint16_t default_track;
    bool loop_on_start;
    bool start_in_low_power;
};

struct AudioControlHandles
{
    DfPlayerMini *player;
};

AudioConfig make_default_audio_config();
void initialize_audio_module(const AudioConfig &config, AudioControlHandles &handles);
