#pragma once

#include "led.h"
#include <vector>
#include <initializer_list>

class LedGroup
{
public:
    LedGroup() = default;
    
    // Allow construction from a list of Leds if needed, or just methods to add
    void add_led(const Led &led)
    {
        leds_.push_back(led);
    }

    std::size_t size() const
    {
        return leds_.size();
    }

    Led &operator[](std::size_t index)
    {
        return leds_[index];
    }

    const Led &operator[](std::size_t index) const
    {
        return leds_[index];
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

    void set_color_for_subset(std::size_t start_index, std::size_t count, const HsvColor &color)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (start_index + i < leds_.size())
            {
                leds_[start_index + i].set_hsv(color);
            }
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
