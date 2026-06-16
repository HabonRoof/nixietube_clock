#pragma once

#include "power_switch_driver.h"
#include "driver/gpio.h"

class GpioPowerSwitch : public IPowerSwitchDriver
{
public:
    GpioPowerSwitch(gpio_num_t hv_pin = GPIO_NUM_15, gpio_num_t dfplayer_pin = GPIO_NUM_16);

    bool init() override;
    bool set_hv_enabled(bool enabled) override;
    bool set_dfplayer_enabled(bool enabled) override;
    bool get_state(PowerSwitchState &state) const override;

private:
    gpio_num_t hv_pin_;
    gpio_num_t dfplayer_pin_;
    bool hv_enabled_;
    bool dfplayer_enabled_;
};
