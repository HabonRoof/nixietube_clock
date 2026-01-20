#include "nixie_driver.h"

NixieDriver::NixieDriver()
{
    // tubes_ is initialized by default constructor
}

void NixieDriver::display_time(uint8_t h, uint8_t m, uint8_t s)
{
    if (tubes_.size() >= 6) {
        tubes_[0].set_numeral(h / 10);
        tubes_[1].set_numeral(h % 10);
        tubes_[2].set_numeral(m / 10);
        tubes_[3].set_numeral(m % 10);
        tubes_[4].set_numeral(s / 10);
        tubes_[5].set_numeral(s % 10);
    }
}

void NixieDriver::display_number(uint32_t number)
{
    for (int i = tubes_.size() - 1; i >= 0; --i) {
        tubes_[i].set_numeral(number % 10);
        number /= 10;
    }
}

void NixieDriver::set_brightness(uint8_t brightness)
{
    // Placeholder: NixieTube class doesn't seem to have direct brightness control for the tube itself yet,
    // only for the backlight. If the tube driver supports PWM/HV control, it goes here.
}

std::vector<NixieTube *> NixieDriver::get_tubes()
{
    std::vector<NixieTube *> ptrs;
    ptrs.reserve(tubes_.size());
    for (auto &tube : tubes_) {
        ptrs.push_back(&tube);
    }
    return ptrs;
}