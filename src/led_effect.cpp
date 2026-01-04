#include "led_effect.h"

#include "esp_log.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr const char *kLogTag = "led_effect";
constexpr float kTwoPi = 6.28318530718f;
constexpr TickType_t kDefaultFrameDelay = pdMS_TO_TICKS(10);
} // namespace

LedEffect::LedEffect(const char *name, ILedStrip &strip)
    : name_(name),
      strip_(strip),
    targets_(),
    back_light_{},
    enabled_(false),
    mutex_(xSemaphoreCreateMutex())
{
    back_light_.color = {0, 0, 0};
    back_light_.brightness = 0;
}

LedEffect::~LedEffect()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void LedEffect::set_enabled(bool enabled)
{
    if (!mutex_)
    {
        enabled_ = enabled;
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    enabled_ = enabled;
    xSemaphoreGive(mutex_);
}

bool LedEffect::is_enabled() const
{
    if (!mutex_)
    {
        return enabled_;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool value = enabled_;
    xSemaphoreGive(mutex_);
    return value;
}

void LedEffect::set_targets(const std::vector<NixieTube *> &digits)
{
    if (!mutex_)
    {
        targets_ = digits;
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    targets_ = digits;
    xSemaphoreGive(mutex_);
}

void LedEffect::set_base_backlight(const BackLightState &back_light)
{
    if (!mutex_)
    {
        back_light_ = back_light;
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    back_light_ = back_light;
    xSemaphoreGive(mutex_);
}

BackLightState LedEffect::base_backlight() const
{
    BackLightState copy = back_light_;

    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        copy = back_light_;
        xSemaphoreGive(mutex_);
    }

    return copy;
}

void LedEffect::tick(uint32_t dt_ms)
{
    if (is_enabled())
    {
        run(dt_ms);
    }
}

const char *LedEffect::name() const
{
    return name_;
}

std::vector<NixieTube *> LedEffect::targets_copy() const
{
    if (!mutex_)
    {
        return targets_;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    auto copy = targets_;
    xSemaphoreGive(mutex_);
    return copy;
}

ILedStrip &LedEffect::strip()
{
    return strip_;
}

SemaphoreHandle_t LedEffect::mutex_handle() const
{
    return mutex_;
}

void LedEffect::apply_backlight_to_targets(const BackLightState &back_light)
{
    auto targets = targets_copy();
    for (auto *digit : targets)
    {
        if (digit != nullptr)
        {
            digit->update_back_light(back_light);
        }
    }
}

BreathEffect::BreathEffect(const char *name, ILedStrip &strip)
    : LedEffect(name, strip),
      speed_hz_(0.35f),
      phase_(0.0f),
      max_brightness_(0)
{
}

void BreathEffect::set_speed(float speed_hz)
{
    float clamped = std::max(speed_hz, 0.05f);
    if (!mutex_handle())
    {
        speed_hz_ = clamped;
        return;
    }

    xSemaphoreTake(mutex_handle(), portMAX_DELAY);
    speed_hz_ = clamped;
    xSemaphoreGive(mutex_handle());
}

void BreathEffect::set_max_brightness(uint8_t max_brightness)
{
    if (!mutex_handle())
    {
        max_brightness_ = max_brightness;
        return;
    }

    xSemaphoreTake(mutex_handle(), portMAX_DELAY);
    max_brightness_ = max_brightness;
    xSemaphoreGive(mutex_handle());
}

void BreathEffect::run(uint32_t dt_ms)
{
    float speed = speed_hz_;
    uint8_t max_brightness = max_brightness_;

    if (mutex_handle())
    {
        xSemaphoreTake(mutex_handle(), portMAX_DELAY);
        speed = speed_hz_;
        max_brightness = max_brightness_;
        xSemaphoreGive(mutex_handle());
    }

    if (max_brightness == 0)
    {
        max_brightness = base_backlight().brightness;
    }

    phase_ += speed * static_cast<float>(dt_ms) * kTwoPi / 1000.0f;
    if (phase_ > kTwoPi)
    {
        phase_ = std::fmod(phase_, kTwoPi);
    }

    float normalized = (std::sin(phase_) + 1.0f) * 0.5f;
    auto back_light = base_backlight();
    back_light.brightness = static_cast<uint8_t>(std::round(normalized * max_brightness));
    apply_backlight_to_targets(back_light);
}

RainbowEffect::RainbowEffect(const char *name, ILedStrip &strip)
    : LedEffect(name, strip),
      speed_deg_per_sec_(60.0f),
      hue_(0.0f)
{
}

void RainbowEffect::set_speed(float degrees_per_second)
{
    if (!mutex_handle())
    {
        speed_deg_per_sec_ = std::max(0.0f, degrees_per_second);
        return;
    }

    xSemaphoreTake(mutex_handle(), portMAX_DELAY);
    speed_deg_per_sec_ = std::max(0.0f, degrees_per_second);
    xSemaphoreGive(mutex_handle());
}

void RainbowEffect::run(uint32_t dt_ms)
{
    float speed = speed_deg_per_sec_;

    if (mutex_handle())
    {
        xSemaphoreTake(mutex_handle(), portMAX_DELAY);
        speed = speed_deg_per_sec_;
        xSemaphoreGive(mutex_handle());
    }

    hue_ += speed * static_cast<float>(dt_ms) / 1000.0f;
    if (hue_ >= 360.0f)
    {
        hue_ = std::fmod(hue_, 360.0f);
    }

    auto back_light = base_backlight();
    back_light.color.hue = static_cast<uint16_t>(hue_) % 360;
    apply_backlight_to_targets(back_light);
}

LedBeatsEffect::LedBeatsEffect(const char *name, ILedStrip &strip)
    : LedEffect(name, strip)
    // TODO: implement led beat effect
{
}

LedService::LedService(ILedStrip &strip, TickType_t frame_delay_ticks)
    : strip_(strip),
      backlight_effect_(nullptr),
      beats_effect_(nullptr),
      backlight_enabled_(true),
      beats_enabled_(true),
      frame_delay_ticks_(frame_delay_ticks == 0 ? kDefaultFrameDelay : frame_delay_ticks),
      task_handle_(nullptr),
      mutex_(xSemaphoreCreateMutex())
{
}

LedService::~LedService()
{
    stop();
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void LedService::start()
{
    if (task_handle_ != nullptr)
    {
        return;
    }

    xTaskCreate(task_entry, "led_service", 4096, this, tskIDLE_PRIORITY + 1, &task_handle_);
}

void LedService::stop()
{
    if (task_handle_ != nullptr)
    {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

void LedService::set_effect(LedEffectCategory category, LedEffect *effect)
{
    if (!mutex_)
    {
        if (category == LedEffectCategory::kBacklight)
        {
            backlight_effect_ = effect;
        }
        else
        {
            beats_effect_ = effect;
        }
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (category == LedEffectCategory::kBacklight)
    {
        backlight_effect_ = effect;
    }
    else
    {
        beats_effect_ = effect;
    }
    xSemaphoreGive(mutex_);
}

void LedService::enable_category(LedEffectCategory category, bool enabled)
{
    if (!mutex_)
    {
        if (category == LedEffectCategory::kBacklight)
        {
            backlight_enabled_ = enabled;
        }
        else
        {
            beats_enabled_ = enabled;
        }
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (category == LedEffectCategory::kBacklight)
    {
        backlight_enabled_ = enabled;
    }
    else
    {
        beats_enabled_ = enabled;
    }
    xSemaphoreGive(mutex_);
}

void LedService::set_frame_delay(TickType_t frame_delay_ticks)
{
    if (!mutex_)
    {
        frame_delay_ticks_ = (frame_delay_ticks == 0) ? kDefaultFrameDelay : frame_delay_ticks;
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    frame_delay_ticks_ = (frame_delay_ticks == 0) ? kDefaultFrameDelay : frame_delay_ticks;
    xSemaphoreGive(mutex_);
}

void LedService::task_entry(void *param)
{
    auto *instance = static_cast<LedService *>(param);
    if (instance != nullptr)
    {
        instance->loop();
    }
    vTaskDelete(nullptr);
}

void LedService::loop()
{
    ESP_LOGI(kLogTag, "Starting LED service loop");
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t last_tick = last_wake_time;

    while (true)
    {
        TickType_t now = xTaskGetTickCount();
        uint32_t dt_ms = pdTICKS_TO_MS(now - last_tick);
        last_tick = now;

        LedEffect *backlight = nullptr;
        LedEffect *beats = nullptr;
        bool backlight_enabled = false;
        bool beats_enabled = false;
        TickType_t frame_delay = kDefaultFrameDelay;

        if (mutex_)
        {
            xSemaphoreTake(mutex_, portMAX_DELAY);
            backlight = backlight_effect_;
            beats = beats_effect_;
            backlight_enabled = backlight_enabled_;
            beats_enabled = beats_enabled_;
            frame_delay = frame_delay_ticks_;
            xSemaphoreGive(mutex_);
        }

        bool updated = false;

        if (backlight_enabled && backlight != nullptr && backlight->is_enabled())
        {
            backlight->tick(dt_ms);
            updated = true;
        }

        if (beats_enabled && beats != nullptr && beats->is_enabled())
        {
            beats->tick(dt_ms);
            updated = true;
        }

        if (updated)
        {
            strip_.show();
        }

        vTaskDelayUntil(&last_wake_time, frame_delay);
    }
}
