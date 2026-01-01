#include "nixie_digit.h"

#include <algorithm>

namespace
{
DigitState make_default_state()
{
    BreathConfig breath{
        .enabled = false,
        .speed = 1.0f,
    };
    SpinConfig spin{
        .direction = SpinDirection::kClockwise,
        .speed = 1.0f,
    };
    LedAnimationConfig animation{
        .mode = LedMode::kOff,
        .breath = breath,
        .spin = spin,
    };
    BackLightState back_light{
        .color = HsvColor{0, 255, 0},
        .brightness = 0,
        .animation = animation,
    };
    DigitState state{
        .numeral = 0,
        .nixie_brightness = 0,
        .back_light = back_light,
    };
    return state;
}
} // namespace

NixieDigit::NixieDigit()
    : mutex_(xSemaphoreCreateMutex()),
      state_(make_default_state())
{
}

NixieDigit::~NixieDigit()
{
    if (mutex_ != nullptr)
    {
        vSemaphoreDelete(mutex_);
    }
}

void NixieDigit::set_state(const DigitState &state)
{
    lock();
    state_ = state;
    unlock();
}

DigitState NixieDigit::get_state() const
{
    lock();
    DigitState copy = state_;
    unlock();
    return copy;
}

void NixieDigit::set_numeral(uint8_t numeral)
{
    lock();
    state_.numeral = numeral;
    unlock();
}

void NixieDigit::update_back_light(const BackLightState &back_light)
{
    lock();
    state_.back_light = back_light;
    unlock();
}

void NixieDigit::lock() const
{
    if (mutex_ != nullptr)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
}

void NixieDigit::unlock() const
{
    if (mutex_ != nullptr)
    {
        xSemaphoreGive(mutex_);
    }
}
