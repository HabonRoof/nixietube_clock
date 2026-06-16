#pragma once

#include "power_switch_driver.h"

class PowerController
{
public:
    explicit PowerController(IPowerSwitchDriver &driver);

    bool init();
    bool set_hv_enabled(bool enabled);
    bool set_dfplayer_enabled(bool enabled);

private:
    IPowerSwitchDriver &driver_;
};
