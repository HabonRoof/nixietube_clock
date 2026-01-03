#pragma once

#include "color_model.h"
#include "led_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

enum class LedMode
{
    kOff,
    kStaticColor,
    kDigitRainbowFlow,
    kPixelSpin,
    kGroupRainbow,
    kBreathing,
};

enum class SpinDirection
{
    kClockwise,
    kCounterClockwise,
};

struct BreathConfig
{
    bool enabled;
    float speed;
};

struct SpinConfig
{
    SpinDirection direction;
    float speed;
};

struct LedAnimationConfig
{
    LedMode mode;
    BreathConfig breath;
    SpinConfig spin;
};

struct BackLightState
{
    HsvColor color;
    uint8_t brightness;
    LedAnimationConfig animation;
};

struct DigitState
{
    uint8_t numeral;
    uint8_t nixie_brightness;
    BackLightState back_light;
};

class NixieDigit
{
public:
    NixieDigit();
    ~NixieDigit();
    NixieDigit(const NixieDigit &) = delete;
    NixieDigit &operator=(const NixieDigit &) = delete;

    void set_state(const DigitState &state);
    DigitState get_state() const;
    void set_numeral(uint8_t numeral);
    void update_back_light(const BackLightState &back_light);

    void attach_led_group(const LedGroup &group);
    void update(uint32_t dt_ms);

private:
    void lock() const;
    void unlock() const;

    mutable SemaphoreHandle_t mutex_;
    DigitState state_;
    LedGroup led_group_;

    // Animation state
    float animation_phase_;
    float spin_phase_;
};
