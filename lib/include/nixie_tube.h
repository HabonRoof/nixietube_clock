#pragma once

#include "freertos/FreeRTOS.h"
#include <cstdint>

struct DigitState
{
    uint8_t numeral;
    uint8_t nixie_brightness;
};

class NixieTube
{
public:
    NixieTube();
    ~NixieTube();
    NixieTube(const NixieTube &) = delete;
    NixieTube &operator=(const NixieTube &) = delete;

    void set_state(const DigitState &state);
    DigitState get_state() const;
    void set_numeral(uint8_t numeral);

private:
    DigitState state_;
};
