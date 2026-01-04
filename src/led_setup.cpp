#include "led_setup.h"
#include "nixie_array.h"

namespace
{
NixieTubeArray g_tubes;
} // namespace

NixieTubeArray &nixie_tube_array()
{
    return g_tubes;
}

BackLightState make_default_backlight(uint8_t brightness)
{
    BackLightState back_light{};
    back_light.color = HsvColor{35, 255, 255};
    back_light.brightness = brightness;
    return back_light;
}

void initialize_led_module(Ws2812Strip &strip, const BackLightState &base_backlight, LedControlHandles &handles)
{
    g_tubes.attach_led_groups(strip, Ws2812Strip::kGroupSize);
    g_tubes.seed_backlight(base_backlight);

    static LedService led_service(strip);
    static BreathEffect backlight_effect("backlight_breath", strip);
    static RainbowEffect rainbow_effect("backlight_rainbow", strip);

    auto all_digits = g_tubes.tube_pointers();

    backlight_effect.set_targets(all_digits);
    backlight_effect.set_base_backlight(base_backlight);
    backlight_effect.set_max_brightness(base_backlight.brightness);
    backlight_effect.set_speed(0.35f);
    backlight_effect.set_enabled(true);

    rainbow_effect.set_targets(all_digits);
    rainbow_effect.set_base_backlight(base_backlight);
    rainbow_effect.set_speed(120.0f);
    rainbow_effect.set_enabled(false); // swap in via LedService when desired

    led_service.set_effect(LedEffectCategory::kBacklight, &backlight_effect);
    led_service.enable_category(LedEffectCategory::kBeats, false); // keep beats reserved for later
    led_service.start();

    handles = LedControlHandles{
        .service = &led_service,
        .backlight = &backlight_effect,
        .rainbow = &rainbow_effect,
    };
}
