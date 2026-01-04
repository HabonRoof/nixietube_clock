#pragma once

#include "nixie_array.h"
#include "ws2812.h"
#include "led_effect.h"

struct LedControlHandles
{
    LedService *service;
    BreathEffect *backlight;
    RainbowEffect *rainbow;
};

NixieTubeArray &nixie_tube_array();

BackLightState make_default_backlight(uint8_t brightness);
void initialize_led_module(Ws2812Strip &strip, const BackLightState &base_backlight, LedControlHandles &handles);
