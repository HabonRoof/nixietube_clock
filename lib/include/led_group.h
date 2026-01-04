#pragma once

#include "led.h"
#include <vector>
#include <initializer_list>

class LedGroup
{
public:
    LedGroup() = default;

    void add_led(const Led &led)
    {
        leds_.push_back(led);
    }

    std::size_t size() const
    {
        return leds_.size();
    }

    bool empty() const
    {
        return leds_.empty();
    }

    void set_hsv(const HsvColor &hsv)
    {
        for (auto &led : leds_)
        {
            led.set_hsv(hsv);
        }
    }

    void set_rgb(const RgbColor &rgb)
    {
        for (auto &led : leds_)
        {
            led.set_rgb(rgb);
        }
    }

    void turn_off()
    {
        for (auto &led : leds_)
        {
            led.turn_off();
        }
    }

    // Iterators
    using iterator = std::vector<Led>::iterator;
    using const_iterator = std::vector<Led>::const_iterator;

    iterator begin() { return leds_.begin(); }
    iterator end() { return leds_.end(); }
    const_iterator begin() const { return leds_.begin(); }
    const_iterator end() const { return leds_.end(); }

private:
    std::vector<Led> leds_;
};
