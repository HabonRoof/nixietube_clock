#include "led_back_light.h"
#include "nixie_digit.h"
#include "ws2812.h"

#include <array>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

std::array<NixieDigit, Ws2812Strip::kGroupCount> g_digits;

void initialize_digits()
{
    for (std::size_t index = 0; index < g_digits.size(); ++index)
    {
        bool use_rainbow = (index % 2 == 0);
        BreathConfig breath{
            .enabled = use_rainbow,
            .speed = 1.0f + 0.2f * static_cast<float>(index),
        };
        SpinConfig spin{
            .direction = (index % 2 == 0) ? SpinDirection::kClockwise : SpinDirection::kCounterClockwise,
            .speed = 1.0f,
        };
        LedAnimationConfig animation{
            .mode = LedMode::kDigitRainbowFlow,
            .breath = breath,
            .spin = spin,
        };
        HsvColor base_color{static_cast<uint16_t>((index * 45) % 360), 255, 255};
        BackLightState back_light{
            .color = base_color,
            .brightness = static_cast<uint8_t>(use_rainbow ? 220 : 255),
            .animation = animation,
        };
        DigitState state{
            .numeral = static_cast<uint8_t>(index),
            .nixie_brightness = 255,
            .back_light = back_light,
        };
        g_digits[index].set_state(state);
    }
}

extern "C" void app_main(void)
{
    initialize_digits();
    LedBackLight::run(g_digits.data(), g_digits.size());
}
