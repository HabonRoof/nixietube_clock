#pragma once

#include "nixie_tube.h"
#include "led_interface.h"
#include "ws2812.h"

#include <array>
#include <vector>

class NixieTubeArray
{
public:
    using TubeArray = std::array<NixieTube, Ws2812Strip::kGroupCount>;

    TubeArray &tubes();
    const TubeArray &tubes() const;

    void attach_led_groups(ILedStrip &strip, std::size_t leds_per_tube);
    void seed_backlight(const BackLightState &back_light);
    std::vector<NixieTube *> tube_pointers();

private:
    TubeArray tubes_;
};
