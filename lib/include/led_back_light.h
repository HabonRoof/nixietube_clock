#pragma once

#pragma once

#include "nixie_digit.h"
#include <cstddef>

namespace LedBackLight
{
void run(NixieDigit *digits, std::size_t digit_count);
}
