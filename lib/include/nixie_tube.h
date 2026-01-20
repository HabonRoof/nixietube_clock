#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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
    void lock() const;
    void unlock() const;

    mutable SemaphoreHandle_t mutex_;
    DigitState state_;
};
