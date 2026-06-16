#pragma once

#include <cstdint>

struct PowerSwitchState {
    bool hv_enabled;
    bool dfplayer_enabled;
};

class IPowerSwitchDriver
{
public:
    virtual ~IPowerSwitchDriver() = default;

    virtual bool init() = 0;
    virtual bool set_hv_enabled(bool enabled) = 0;
    virtual bool set_dfplayer_enabled(bool enabled) = 0;
    virtual bool get_state(PowerSwitchState &state) const = 0;
};
