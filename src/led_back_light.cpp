#include "led_back_light.h"

#include "color_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nixie_digit.h"
#include "ws2812.h"
#include <algorithm>
#include <cmath>

namespace LedBackLight
{
namespace
{
constexpr gpio_num_t kOnboardLedPin = static_cast<gpio_num_t>(39);
constexpr TickType_t kFrameDelayTicks = pdMS_TO_TICKS(10);
constexpr uint16_t kHueIncrement = 3;
constexpr uint16_t kGroupHueStep = 20;
constexpr float kBreathDelta = 0.05f;
constexpr float kTwoPi = 6.28318530718f;
constexpr uint8_t kSpinPatternPercent[Ws2812Strip::kGroupSize] = {100, 80, 60, 40};
constexpr float kDefaultSpinIntervalMs = 120.0f;
constexpr float kMinSpinIntervalMs = 10.0f;
constexpr float kMinSpinSpeed = 0.1f;

const char *kAppLogTag = "led_back_light";

class Controller
{
public:
    Controller(NixieDigit *digits, std::size_t digit_count)
        : led_strip_(kOnboardLedPin),
          digits_(digits),
          digit_count_(digit_count),
          base_hue_(0),
          breath_phase_(0.0f),
          elapsed_ticks_(0)
    {
    }

    void run()
    {
        ESP_LOGI(kAppLogTag, "Starting LED backlight controller (digits=%zu)", digit_count_);
        while (true)
        {
            render_frame();
            advance_animation();
            vTaskDelay(kFrameDelayTicks);
        }
    }

private:
    void render_frame()
    {
        if (!digits_ || digit_count_ == 0)
        {
            return;
        }

        for (std::size_t digit_index = 0; digit_index < digit_count_; ++digit_index)
        {
            DigitState state = digits_[digit_index].get_state();
            render_digit(digit_index, state);
        }

        esp_err_t status = led_strip_.show();
        if (status != ESP_OK)
        {
            ESP_LOGE(kAppLogTag, "WS2812 show failed: %d", status);
        }
    }

    void render_digit(std::size_t digit_index, const DigitState &state)
    {
        const LedAnimationConfig &animation = state.back_light.animation;
        uint8_t brightness = state.back_light.brightness;
        if (animation.breath.enabled)
        {
            brightness = compute_breath_brightness(animation, brightness);
        }

        switch (animation.mode)
        {
        case LedMode::kOff:
            led_strip_.set_group(digit_index, 0, 0, 0);
            break;
        case LedMode::kStaticColor:
            render_static_color(digit_index, state, brightness);
            break;
        case LedMode::kDigitRainbowFlow:
            render_group_rainbow(digit_index, state, brightness);
            break;
        case LedMode::kPixelSpin:
            render_digit_spin(digit_index, state, brightness);
            break;
        }
    }

    void render_static_color(std::size_t digit_index, const DigitState &state, uint8_t brightness)
    {
        HsvColor hsv = state.back_light.color;
        hsv.value = brightness;
        RgbColor rgb = create_rgb_from_hsv(hsv);
        esp_err_t status = led_strip_.set_group(digit_index, rgb.red, rgb.green, rgb.blue);
        if (status != ESP_OK)
        {
            ESP_LOGE(kAppLogTag, "set_group(static) failed digit=%zu err=%d", digit_index, status);
        }
    }

    void render_group_rainbow(std::size_t digit_index, const DigitState &state, uint8_t brightness)
    {
        uint16_t hue = static_cast<uint16_t>((base_hue_ + digit_index * kGroupHueStep) % 360);
        HsvColor hsv{
            .hue = hue,
            .saturation = state.back_light.color.saturation,
            .value = brightness,
        };
        RgbColor rgb = create_rgb_from_hsv(hsv);
        esp_err_t status = led_strip_.set_group(digit_index, rgb.red, rgb.green, rgb.blue);
        if (status != ESP_OK)
        {
            ESP_LOGE(kAppLogTag, "set_group(rainbow) failed digit=%zu err=%d", digit_index, status);
        }
    }

    void render_digit_spin(std::size_t digit_index, const DigitState &state, uint8_t base_brightness)
    {
        const SpinConfig &spin = state.back_light.animation.spin;
        float speed = (spin.speed > kMinSpinSpeed) ? spin.speed : kMinSpinSpeed;
        float interval_ms = kDefaultSpinIntervalMs / speed;
        if (interval_ms < kMinSpinIntervalMs)
        {
            interval_ms = kMinSpinIntervalMs;
        }
        TickType_t interval_ticks = pdMS_TO_TICKS(static_cast<uint32_t>(interval_ms));
        if (interval_ticks == 0)
        {
            interval_ticks = 1;
        }
        uint32_t step = static_cast<uint32_t>((elapsed_ticks_ / interval_ticks) % Ws2812Strip::kGroupSize);
        if (spin.direction == SpinDirection::kCounterClockwise && step != 0)
        {
            step = (Ws2812Strip::kGroupSize - step) % Ws2812Strip::kGroupSize;
        }

        for (std::size_t pixel = 0; pixel < Ws2812Strip::kGroupSize; ++pixel)
        {
            std::size_t led = digit_index * Ws2812Strip::kGroupSize + pixel;
            uint8_t pattern_percent = kSpinPatternPercent[(pixel + step) % Ws2812Strip::kGroupSize];
            uint16_t scaled_value = static_cast<uint16_t>(base_brightness) * pattern_percent / 100;
            uint8_t final_value = static_cast<uint8_t>(std::min<uint16_t>(scaled_value, 255));
            HsvColor hsv = state.back_light.color;
            hsv.value = final_value;
            RgbColor rgb = create_rgb_from_hsv(hsv);
            esp_err_t status = led_strip_.set_pixel(led, rgb.red, rgb.green, rgb.blue);
            if (status != ESP_OK)
            {
                ESP_LOGE(kAppLogTag, "set_pixel(spin) failed led=%zu err=%d", led, status);
                return;
            }
        }
    }

    RgbColor create_rgb_from_hsv(const HsvColor &hsv) const
    {
        RgbColor linear = hsv_to_rgb(hsv);
        return apply_gamma(linear);
    }

    uint8_t compute_breath_brightness(const LedAnimationConfig &animation, uint8_t base_brightness) const
    {
        const BreathConfig &breath = animation.breath;
        if (base_brightness == 0)
        {
            return 0;
        }
        float speed = (breath.speed > 0.0f) ? breath.speed : 1.0f;
        float phase = breath_phase_ * speed;
        float normalized = 0.5f * (std::sin(phase) + 1.0f);
        float value = normalized * static_cast<float>(base_brightness);
        if (value < 0.0f)
        {
            value = 0.0f;
        }
        if (value > 255.0f)
        {
            value = 255.0f;
        }
        return static_cast<uint8_t>(value + 0.5f);
    }

    void advance_animation()
    {
        base_hue_ = static_cast<uint16_t>((base_hue_ + kHueIncrement) % 360);
        breath_phase_ += kBreathDelta;
        if (breath_phase_ > kTwoPi)
        {
            breath_phase_ -= kTwoPi;
        }
        elapsed_ticks_ += kFrameDelayTicks;
    }

    Ws2812Strip led_strip_;
    NixieDigit *digits_;
    std::size_t digit_count_;
    uint16_t base_hue_;
    float breath_phase_;
    TickType_t elapsed_ticks_;
};
} // namespace

void run(NixieDigit *digits, std::size_t digit_count)
{
    Controller controller(digits, digit_count);
    controller.run();
}

} // namespace LedBackLight
