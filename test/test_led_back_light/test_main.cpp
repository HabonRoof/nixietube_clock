#include <unity.h>

#include "color_model.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nixie_tube.h"

void setUp() {}
void tearDown() {}

void test_hsv_to_rgb_red()
{
    HsvColor hsv{0, 255, 255};
    RgbColor rgb = hsv_to_rgb(hsv);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.red);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.green);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.blue);
}

void test_hsv_to_rgb_green()
{
    HsvColor hsv{120, 255, 255};
    RgbColor rgb = hsv_to_rgb(hsv);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.red);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.green);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.blue);
}

void test_hsv_to_rgb_blue()
{
    HsvColor hsv{240, 255, 255};
    RgbColor rgb = hsv_to_rgb(hsv);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.red);
    TEST_ASSERT_EQUAL_UINT8(0, rgb.green);
    TEST_ASSERT_EQUAL_UINT8(255, rgb.blue);
}

void test_hsv_to_rgb_white()
{
    HsvColor hsv{0, 0, 200};
    RgbColor rgb = hsv_to_rgb(hsv);
    TEST_ASSERT_EQUAL_UINT8(200, rgb.red);
    TEST_ASSERT_EQUAL_UINT8(200, rgb.green);
    TEST_ASSERT_EQUAL_UINT8(200, rgb.blue);
}

void test_gamma_extremes()
{
    RgbColor dark{0, 0, 0};
    RgbColor bright{255, 255, 255};
    RgbColor corrected_dark = apply_gamma(dark);
    RgbColor corrected_bright = apply_gamma(bright);
    TEST_ASSERT_EQUAL_UINT8(0, corrected_dark.red);
    TEST_ASSERT_EQUAL_UINT8(0, corrected_dark.green);
    TEST_ASSERT_EQUAL_UINT8(0, corrected_dark.blue);
    TEST_ASSERT_EQUAL_UINT8(255, corrected_bright.red);
    TEST_ASSERT_EQUAL_UINT8(255, corrected_bright.green);
    TEST_ASSERT_EQUAL_UINT8(255, corrected_bright.blue);
}

void test_nixie_digit_state_roundtrip()
{
    NixieTube digit;
    BackLightState back_light{
        .color = HsvColor{90, 200, 180},
        .brightness = 180,
    };
    DigitState expected{
        .numeral = 7,
        .nixie_brightness = 150,
        .back_light = back_light,
    };
    digit.set_state(expected);
    DigitState actual = digit.get_state();
    TEST_ASSERT_EQUAL_UINT8(expected.numeral, actual.numeral);
    TEST_ASSERT_EQUAL_UINT8(expected.nixie_brightness, actual.nixie_brightness);
    TEST_ASSERT_EQUAL_UINT16(expected.back_light.color.hue, actual.back_light.color.hue);
    TEST_ASSERT_EQUAL_UINT8(expected.back_light.color.saturation, actual.back_light.color.saturation);
    TEST_ASSERT_EQUAL_UINT8(expected.back_light.color.value, actual.back_light.color.value);
    TEST_ASSERT_EQUAL_UINT8(expected.back_light.brightness, actual.back_light.brightness);
}

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    UNITY_BEGIN();
    RUN_TEST(test_hsv_to_rgb_red);
    RUN_TEST(test_hsv_to_rgb_green);
    RUN_TEST(test_hsv_to_rgb_blue);
    RUN_TEST(test_hsv_to_rgb_white);
    RUN_TEST(test_gamma_extremes);
    RUN_TEST(test_nixie_digit_state_roundtrip);
    UNITY_END();
}
