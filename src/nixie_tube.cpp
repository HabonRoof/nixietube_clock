#include "nixie_tube.h"

NixieTube::NixieTube()
    : mutex_(xSemaphoreCreateMutex()),
      state_{}
{
    state_.numeral = 0;
    state_.nixie_brightness = 0;
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
