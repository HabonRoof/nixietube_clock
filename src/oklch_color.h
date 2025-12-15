#pragma once

#include <cstdint>

struct LightColor
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct LightColorGain
{
    float red_gain;
    float green_gain;
    float blue_gain;
};

class OklchColorGenerator
{
public:
    OklchColorGenerator();

    void set_rgb_gain(float red_gain, float green_gain, float blue_gain);
    void set_d65_defaults();
    LightColor convert(uint8_t hue_value, uint8_t chroma_value, uint8_t brightness_value) const;
    LightColor apply_gamma(const LightColor &linear_color) const;

private:
    static void convert_oklab_to_linear_srgb(float lightness, float component_a, float component_b,
                                             float &red_linear, float &green_linear, float &blue_linear);

    LightColorGain color_gain_;
};
