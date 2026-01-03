#include "nixie_digit.h"

#include <algorithm>
#include <cmath>
#include "freertos/task.h"

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

constexpr float kBreathDelta = 0.05f;
constexpr float kTwoPi = 6.28318530718f;
constexpr uint8_t kSpinPatternPercent[] = {100, 80, 60, 40};
constexpr float kDefaultSpinIntervalMs = 120.0f;
constexpr float kMinSpinIntervalMs = 10.0f;
constexpr float kMinSpinSpeed = 0.1f;
constexpr float kRainbowHueSpeed = 3.0f;

// Advance a shared hue so every group stays in sync for the group rainbow effect.
float update_group_rainbow_hue()
{
    static float hue = 0.0f;
    static TickType_t last_tick = 0;

    TickType_t now = xTaskGetTickCount();
    if (last_tick == 0)
    {
        last_tick = now;
        return hue;
    }

    if (now < last_tick)
    {
        last_tick = now;
        return hue;
    }

    TickType_t delta_ticks = now - last_tick;
    if (delta_ticks == 0)
    {
        return hue;
    }

    float delta_ms = static_cast<float>(delta_ticks * portTICK_PERIOD_MS);
    float hue_delta = kRainbowHueSpeed * (delta_ms / 10.0f);
    hue += hue_delta;
    if (hue >= 360.0f)
    {
        hue = std::fmod(hue, 360.0f);
    }

    last_tick = now;
    return hue;
}
} // namespace

NixieDigit::NixieDigit()
    : mutex_(xSemaphoreCreateMutex()),
      state_(make_default_state()),
      animation_phase_(0.0f),
      spin_phase_(0.0f)
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

void NixieDigit::attach_led_group(const LedGroup &group)
{
    lock();
    led_group_ = group;
    unlock();
}

void NixieDigit::update(uint32_t dt_ms)
{
    lock();
    DigitState state = state_;
    unlock();

    const LedAnimationConfig &animation = state.back_light.animation;
    uint8_t brightness = state.back_light.brightness;

    // Breath Animation
    bool apply_breath = animation.breath.enabled || animation.mode == LedMode::kBreathing;
    if (apply_breath)
    {
        float speed = (animation.breath.speed > 0.0f) ? animation.breath.speed : 1.0f;
        float delta = kBreathDelta * (static_cast<float>(dt_ms) / 10.0f) * speed;
        animation_phase_ += delta;
        if (animation_phase_ > kTwoPi)
            animation_phase_ -= kTwoPi;

        float normalized = 0.5f * (std::sin(animation_phase_) + 1.0f);
        float value = normalized * static_cast<float>(brightness);
        brightness = static_cast<uint8_t>(std::max(0.0f, std::min(value, 255.0f)));
    }

    // Prepare color
    HsvColor hsv = state.back_light.color;
    hsv.value = brightness;

    switch (animation.mode)
    {
    case LedMode::kOff:
        led_group_.turn_off();
        break;

    case LedMode::kStaticColor:
    case LedMode::kBreathing:
        led_group_.set_hsv(hsv);
        break;

    case LedMode::kDigitRainbowFlow:
    {
        float hue_delta = kRainbowHueSpeed * (static_cast<float>(dt_ms) / 10.0f);
        spin_phase_ += hue_delta;
        if (spin_phase_ > 360.0f)
            spin_phase_ -= 360.0f;

        hsv.hue = static_cast<uint16_t>(spin_phase_);
        led_group_.set_hsv(hsv);
        break;
    }

    case LedMode::kGroupRainbow:
    {
        float group_hue = update_group_rainbow_hue();
        hsv.hue = static_cast<uint16_t>(group_hue);
        led_group_.set_hsv(hsv);
        break;
    }

    case LedMode::kPixelSpin:
    {
        const SpinConfig &spin = animation.spin;
        float speed = (spin.speed > kMinSpinSpeed) ? spin.speed : kMinSpinSpeed;
        float interval_ms = kDefaultSpinIntervalMs / speed;
        if (interval_ms < kMinSpinIntervalMs)
            interval_ms = kMinSpinIntervalMs;

        spin_phase_ += static_cast<float>(dt_ms);
        if (spin_phase_ >= interval_ms * 4.0f)
            spin_phase_ -= interval_ms * 4.0f;

        int step = static_cast<int>(spin_phase_ / interval_ms) % 4;

        if (spin.direction == SpinDirection::kCounterClockwise)
        {
            step = (4 - step) % 4;
        }

        for (std::size_t i = 0; i < led_group_.size(); ++i)
        {
            std::size_t pattern_idx = (i + step) % 4;
            uint8_t pattern_percent = kSpinPatternPercent[pattern_idx];

            uint16_t val = static_cast<uint16_t>(brightness) * pattern_percent / 100;
            HsvColor pixel_hsv = hsv;
            pixel_hsv.value = static_cast<uint8_t>(std::min<uint16_t>(val, 255));
            if (i < led_group_.size())
                led_group_[i].set_hsv(pixel_hsv);
        }
        break;
    }
    }
}
