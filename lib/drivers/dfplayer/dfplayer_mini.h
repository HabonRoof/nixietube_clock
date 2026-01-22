#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstdint>
#include <map>
#include <string>

enum class DfPlayerEqPreset : uint8_t
{
    kNormal = 0,
    kPop = 1,
    kRock = 2,
    kJazz = 3,
    kClassic = 4,
    kBass = 5,
};

struct AudioPlaybackState
{
    uint8_t volume;
    uint16_t track_number;
    bool looping;
    bool low_power;
    bool paused;
};

class DfPlayerMini
{
public:
    explicit DfPlayerMini(uart_port_t uart_num);
    ~DfPlayerMini();
    DfPlayerMini(const DfPlayerMini &) = delete;
    DfPlayerMini &operator=(const DfPlayerMini &) = delete;

    esp_err_t begin(int baud_rate = 9600);
    esp_err_t play_track(uint16_t track_number);
    esp_err_t play_next();
    esp_err_t play_previous();
    esp_err_t pause();
    esp_err_t resume();
    esp_err_t stop();
    esp_err_t set_volume(uint8_t volume);
    esp_err_t volume_up();
    esp_err_t volume_down();
    esp_err_t set_loop(bool enable);
    esp_err_t set_low_power_mode(bool enable);
    esp_err_t set_eq(DfPlayerEqPreset eq);
    esp_err_t reset();

    AudioPlaybackState state() const;
    void set_track_names(const std::map<uint16_t, std::string> &track_names);

private:
    esp_err_t configure_uart(int baud_rate);
    esp_err_t send_simple_command(uint8_t command, uint16_t parameter = 0, bool request_feedback = false);
    static uint16_t calculate_checksum(uint8_t command, uint16_t parameter, bool request_feedback);

    uart_port_t uart_num_;
    int baud_rate_;
    AudioPlaybackState state_;
    std::map<uint16_t, std::string> track_names_;
    mutable SemaphoreHandle_t mutex_;
    bool initialized_;
};
