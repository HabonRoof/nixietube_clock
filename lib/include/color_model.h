#pragma once

#include <cstdint>

struct RgbColor
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct HsvColor
{
    uint16_t hue; // 0-360
    uint8_t saturation; // 0-255
    uint8_t value; // 0-255
};

RgbColor hsv_to_rgb(const HsvColor &hsv);
HsvColor rgb_to_hsv(const RgbColor &rgb);
RgbColor apply_gamma(const RgbColor &linear_color);
