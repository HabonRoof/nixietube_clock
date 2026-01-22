#pragma once

#include "dfplayer_mini.h"
#include "driver/gpio.h"
#include "driver/uart.h"

// Abstract Interface for Audio Driver
class IAudioDriver
{
public:
    virtual ~IAudioDriver() = default;
    virtual esp_err_t play_track(uint16_t track_number) = 0;
    virtual esp_err_t stop() = 0;
    virtual esp_err_t pause() = 0;
    virtual esp_err_t resume() = 0;
    virtual esp_err_t set_volume(uint8_t volume) = 0;
    virtual esp_err_t volume_up() = 0;
    virtual esp_err_t volume_down() = 0;
    virtual esp_err_t play_next() = 0;
    virtual esp_err_t play_previous() = 0;
};

// Concrete Implementation wrapping DfPlayerMini
class AudioDriver : public IAudioDriver
{
public:
    explicit AudioDriver(uart_port_t uart_num);
    ~AudioDriver() override = default;

    esp_err_t play_track(uint16_t track_number) override;
    esp_err_t stop() override;
    esp_err_t pause() override;
    esp_err_t resume() override;
    esp_err_t set_volume(uint8_t volume) override;
    esp_err_t volume_up() override;
    esp_err_t volume_down() override;
    esp_err_t play_next() override;
    esp_err_t play_previous() override;

private:
    DfPlayerMini player_;
};