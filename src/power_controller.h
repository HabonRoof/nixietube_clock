#pragma once

#include "power_switch_driver.h"

class Husb238;

class PowerController
{
public:
    explicit PowerController(IPowerSwitchDriver &driver, Husb238 *pd_sink = nullptr);

    bool init();
    // HV boost stays off until a 12V PD contract is established.
    bool bring_up_hv();
    // Leave the HUSB238 5V default in place and keep the HV boost off.
    void keep_default_5v(const char *reason);
    bool set_hv_enabled(bool enabled);
    bool set_dfplayer_enabled(bool enabled);

    bool pd_bringup_ok() const { return pd_ok_; }
    const char *pd_status_message() const { return pd_status_; }

private:
    void publish_pd_status(bool ok, const char *message);

    IPowerSwitchDriver &driver_;
    Husb238 *pd_sink_;
    bool pd_ok_;
    char pd_status_[160];
};
