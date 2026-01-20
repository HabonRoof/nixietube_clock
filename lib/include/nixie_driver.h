#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include "nixie_tube.h"

// Abstract Interface for Nixie Driver
class INixieDriver
{
public:
    virtual ~INixieDriver() = default;
    virtual void display_time(uint8_t h, uint8_t m, uint8_t s) = 0;
    virtual void display_number(uint32_t number) = 0;
    virtual void set_brightness(uint8_t brightness) = 0; // PWM or similar if supported
    virtual std::vector<NixieTube *> get_tubes() = 0;
};

// Concrete Implementation
class NixieDriver : public INixieDriver
{
public:
    NixieDriver();
    ~NixieDriver() override = default;

    void display_time(uint8_t h, uint8_t m, uint8_t s) override;
    void display_number(uint32_t number) override;
    void set_brightness(uint8_t brightness) override;
    std::vector<NixieTube *> get_tubes() override;

private:
    std::array<NixieTube, 6> tubes_;
};