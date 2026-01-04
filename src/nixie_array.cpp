#include "nixie_array.h"

#include "led_group.h"
#include "led.h"

NixieTubeArray::TubeArray &NixieTubeArray::tubes()
{
    return tubes_;
}

const NixieTubeArray::TubeArray &NixieTubeArray::tubes() const
{
    return tubes_;
}

void NixieTubeArray::attach_led_groups(ILedStrip &strip, std::size_t leds_per_tube)
{
    for (std::size_t i = 0; i < tubes_.size(); ++i)
    {
        LedGroup group;
        for (std::size_t j = 0; j < leds_per_tube; ++j)
        {
            std::size_t led_index = i * leds_per_tube + j;
            if (led_index < strip.get_led_count())
            {
                group.add_led(Led(&strip, led_index));
            }
        }
        tubes_[i].attach_led_group(group);
    }
}

void NixieTubeArray::seed_backlight(const BackLightState &back_light)
{
    for (std::size_t index = 0; index < tubes_.size(); ++index)
    {
        DigitState state{
            .numeral = static_cast<uint8_t>(index),
            .nixie_brightness = 255,
            .back_light = back_light,
        };
        tubes_[index].set_state(state);
    }
}

std::vector<NixieTube *> NixieTubeArray::tube_pointers()
{
    std::vector<NixieTube *> digits;
    digits.reserve(tubes_.size());
    for (auto &digit : tubes_)
    {
        digits.push_back(&digit);
    }
    return digits;
}
