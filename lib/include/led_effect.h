#pragma once

#include "led_interface.h"
#include "nixie_digit.h"
#include <cstddef>

class LedEffect
{
public:
    virtual ~LedEffect() = default;
    virtual void loop() = 0;
};

class DefaultLedEffect : public LedEffect
{
public:
    DefaultLedEffect(ILedStrip &strip, NixieDigit *digits, std::size_t digit_count);
    void loop() override;

private:
    ILedStrip &strip_;
    NixieDigit *digits_;
    std::size_t digit_count_;
};
