#pragma once

#include "color_model.h"
#include "led_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

struct BackLightState
{
    HsvColor color;
    uint8_t brightness;
};

struct DigitState
{
    uint8_t numeral;
    uint8_t nixie_brightness;
    BackLightState back_light;
};

class NixieTube
{
public:
    NixieTube();
    ~NixieTube();
    NixieTube(const NixieTube &) = delete;
    NixieTube &operator=(const NixieTube &) = delete;

    void set_state(const DigitState &state);
    DigitState get_state() const;
    void set_numeral(uint8_t numeral);
    void update_back_light(const BackLightState &back_light);
    void turn_off_back_light();

    void attach_led_group(const LedGroup &group);
    void update(uint32_t dt_ms);

private:
    void lock() const;
    void unlock() const;
    void apply_back_light_locked();

    mutable SemaphoreHandle_t mutex_;
    DigitState state_;
    LedGroup led_group_;
};
