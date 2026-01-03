#include "nixie_digit.h"
#include "ws2812.h"
#include "led_effect.h"
#include "led_group.h"
#include "led.h"

#include <array>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *kLogTag = "main";
static constexpr gpio_num_t kOnboardLedPin = static_cast<gpio_num_t>(39);

std::array<NixieDigit, Ws2812Strip::kGroupCount> g_digits;

void initialize_digits()
{
    for (std::size_t index = 0; index < g_digits.size(); ++index)
    {
        BreathConfig breath{
            .enabled = false,
            .speed = 1.0f,
        };
        SpinConfig spin{
            .direction = (index % 2 == 0) ? SpinDirection::kClockwise : SpinDirection::kCounterClockwise,
            .speed = 1.0f,
        };
        LedAnimationConfig animation{
            .mode = LedMode::kGroupRainbow,
            .breath = breath,
            .spin = spin,
        };
        HsvColor base_color{0, 255, 255};
        BackLightState back_light{
            .color = base_color,
            .brightness = 255,
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
    ESP_LOGI(kLogTag, "Initializing Nixie Clock...");

    // 1. Initialize LED Strip
    static Ws2812Strip led_strip(kOnboardLedPin);

    // 2. Attach LED Groups to Digits
    for (std::size_t i = 0; i < g_digits.size(); ++i)
    {
        LedGroup group;
        for (std::size_t j = 0; j < Ws2812Strip::kGroupSize; ++j)
        {
            // Map logical LED index to physical strip index
            // Assuming strip layout is sequential: digit 0 (0-3), digit 1 (4-7)...
            std::size_t led_index = i * Ws2812Strip::kGroupSize + j;
            if (led_index < led_strip.get_led_count())
            {
                group.add_led(Led(&led_strip, led_index));
            }
        }
        g_digits[i].attach_led_group(group);
    }

    // 3. Initialize Digit States
    initialize_digits();

    // 4. Run Effect Loop
    static DefaultLedEffect effect(led_strip, g_digits.data(), g_digits.size());
    effect.loop(); 
}
