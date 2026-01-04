#include "nixie_tube.h"

#include <algorithm>

NixieTube::NixieTube()
    : mutex_(xSemaphoreCreateMutex()),
      state_{},
      led_group_()
{
    state_.numeral = 0;
    state_.nixie_brightness = 0;
    state_.back_light.color = {0, 0, 0};
    state_.back_light.brightness = 0;
}

NixieTube::~NixieTube()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void NixieTube::set_state(const DigitState &state)
{
    lock();
    state_ = state;
    apply_back_light_locked();
    unlock();
}

DigitState NixieTube::get_state() const
{
    lock();
    DigitState copy = state_;
    unlock();
    return copy;
}

void NixieTube::set_numeral(uint8_t numeral)
{
    lock();
    state_.numeral = numeral;
    unlock();
}

void NixieTube::update_back_light(const BackLightState &back_light)
{
    lock();
    state_.back_light = back_light;
    apply_back_light_locked();
    unlock();
}

void NixieTube::turn_off_back_light()
{
    lock();
    state_.back_light.brightness = 0;
    led_group_.turn_off();
    unlock();
}

void NixieTube::attach_led_group(const LedGroup &group)
{
    lock();
    led_group_ = group;
    apply_back_light_locked();
    unlock();
}

void NixieTube::update(uint32_t /*dt_ms*/)
{
    lock();
    apply_back_light_locked();
    unlock();
}

void NixieTube::lock() const
{
    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

void NixieTube::unlock() const
{
    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

void NixieTube::apply_back_light_locked()
{
    if (led_group_.empty())
    {
        return;
    }

    HsvColor adjusted = state_.back_light.color;
    uint16_t scaled_value = static_cast<uint16_t>(adjusted.value) * state_.back_light.brightness / 255;
    adjusted.value = static_cast<uint8_t>(std::min<uint16_t>(scaled_value, 255));
    led_group_.set_hsv(adjusted);
}
