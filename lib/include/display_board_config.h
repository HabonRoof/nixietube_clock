#pragma once

#include "driver/gpio.h"
#include <cstdint>

// Upper display board type indicator on GPIO19 (DISPLAY_TYPE net).
// Low = IN-4, high = IN-14. Defaults to IN-4 when the pin is low or floating.
enum class DisplayBoardType : uint8_t
{
    IN4 = 0,
    IN14 = 1,
};

constexpr gpio_num_t kDisplayTypePin = GPIO_NUM_19;
constexpr DisplayBoardType kDefaultDisplayBoardType = DisplayBoardType::IN4;

void init_display_type_gpio();
DisplayBoardType get_display_board_type();
const char *display_board_type_name(DisplayBoardType type);
