#include "oklch_color.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kLightnessConstant = 0.50f;
constexpr float kMaximumChroma = 0.30f;
constexpr float kTwoPi = 6.28318530718f;

uint8_t clamp_to_byte(float value)
{
    value = std::max(0.0f, std::min(255.0f, value));
    return static_cast<uint8_t>(std::lround(value));
}

const uint8_t kGammaLookupTable[256] = {
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
    1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
    3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
    6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
    12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
    20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
    30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
    42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
    56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
    73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
    91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
   113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
   137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
   163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
   192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
   223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
};
} // namespace

OklchColorGenerator::OklchColorGenerator()
{
    set_d65_defaults();
}

void OklchColorGenerator::set_rgb_gain(float red_gain, float green_gain, float blue_gain)
{
    color_gain_.red_gain = red_gain;
    color_gain_.green_gain = green_gain;
    color_gain_.blue_gain = blue_gain;
}

void OklchColorGenerator::set_d65_defaults()
{
    color_gain_.red_gain = 1.00f;
    color_gain_.green_gain = 0.96f;
    color_gain_.blue_gain = 0.90f;
}

LightColor OklchColorGenerator::convert(uint8_t hue_value, uint8_t chroma_value, uint8_t brightness_value) const
{
    float chroma = (static_cast<float>(chroma_value) / 255.0f) * kMaximumChroma;
    float hue_radians = (static_cast<float>(hue_value) / 255.0f) * kTwoPi;
    float component_a = chroma * std::cos(hue_radians);
    float component_b = chroma * std::sin(hue_radians);

    float red_linear = 0.0f;
    float green_linear = 0.0f;
    float blue_linear = 0.0f;
    convert_oklab_to_linear_srgb(kLightnessConstant, component_a, component_b, red_linear, green_linear, blue_linear);

    float brightness_scale = static_cast<float>(brightness_value) / 255.0f;
    red_linear *= brightness_scale * color_gain_.red_gain;
    green_linear *= brightness_scale * color_gain_.green_gain;
    blue_linear *= brightness_scale * color_gain_.blue_gain;

    LightColor color{};
    color.red = clamp_to_byte(red_linear * 255.0f);
    color.green = clamp_to_byte(green_linear * 255.0f);
    color.blue = clamp_to_byte(blue_linear * 255.0f);
    return color;
}

LightColor OklchColorGenerator::apply_gamma(const LightColor &linear_color) const
{
    LightColor corrected_color{};
    corrected_color.red = kGammaLookupTable[linear_color.red];
    corrected_color.green = kGammaLookupTable[linear_color.green];
    corrected_color.blue = kGammaLookupTable[linear_color.blue];
    return corrected_color;
}

void OklchColorGenerator::convert_oklab_to_linear_srgb(float lightness, float component_a, float component_b,
                                                       float &red_linear, float &green_linear, float &blue_linear)
{
    float l_component = lightness + 0.3963377774f * component_a + 0.2158037573f * component_b;
    float m_component = lightness - 0.1055613458f * component_a - 0.0638541728f * component_b;
    float s_component = lightness - 0.0894841775f * component_a - 1.2914855480f * component_b;

    l_component = l_component * l_component * l_component;
    m_component = m_component * m_component * m_component;
    s_component = s_component * s_component * s_component;

    red_linear = +4.0767416621f * l_component - 3.3077115913f * m_component + 0.2309699292f * s_component;
    green_linear = -1.2684380046f * l_component + 2.6097574011f * m_component - 0.3413193965f * s_component;
    blue_linear = -0.0041960863f * l_component - 0.7034186147f * m_component + 1.7076147010f * s_component;
}
