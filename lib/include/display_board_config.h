#pragma once

#include <cstdint>

// Upper display board type indicator on GPIO19 (DISPLAY_TYPE net), read as analog voltage:
//   0 V    -> IN-4 (6 tubes)
//   1.65 V -> IN-14 (8 tubes)
//   3.3 V  -> IN-14 (6 tubes)
enum class DisplayBoardType : uint8_t
{
    IN4_6 = 0,
    IN14_8 = 1,
    IN14_6 = 2,
};

constexpr uint8_t kMaxDisplayTubes = 8;
constexpr uint8_t kMaxPca9685Chips = 5;

struct DisplayBoardProfile
{
    DisplayBoardType type;
    const char *name;
    uint8_t tube_count;
    uint8_t pca_chip_count;
    uint8_t pca_addresses[kMaxPca9685Chips];
};

constexpr DisplayBoardType kDefaultDisplayBoardType = DisplayBoardType::IN4_6;

// Must run before 74HC238 MUX and PCA9685 initialization.
void init_display_type_adc();
DisplayBoardType get_display_board_type();
const DisplayBoardProfile &get_display_board_profile();
const char *display_board_type_name(DisplayBoardType type);
