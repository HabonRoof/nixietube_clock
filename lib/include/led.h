#pragma once

#include "led_interface.h"
#include "color_model.h"

class Led
{
public:
    Led(ILedStrip *strip, std::size_t index)
        : strip_(strip), index_(index)
    {
    }

    void set_hsv(const HsvColor &hsv)
    {
        RgbColor rgb = hsv_to_rgb(hsv);
        rgb = apply_gamma(rgb);
        set_rgb(rgb);
    }

    void set_rgb(const RgbColor &rgb)
    {
        if (strip_)
        {
            strip_->set_pixel(index_, rgb.red, rgb.green, rgb.blue);
        }
    }

    void turn_off()
    {
        set_rgb({0, 0, 0});
    }

private:
    ILedStrip *strip_;
    std::size_t index_;
};
