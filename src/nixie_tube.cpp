#include "nixie_tube.h"

NixieTube::NixieTube():
    state_{}
{
    state_.numeral = 0;
    state_.nixie_brightness = 0;
}

NixieTube::~NixieTube()
{
    // Do nothing, should destruct I2C driver for PCA9685
}

void NixieTube::set_state(const DigitState &state)
{
    state_ = state;
}

DigitState NixieTube::get_state() const
{
    DigitState copy = state_;
    return copy;
}

void NixieTube::set_numeral(uint8_t numeral)
{
    state_.numeral = numeral;
}

