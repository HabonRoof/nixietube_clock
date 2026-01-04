#pragma once

#include "led_interface.h"
#include "nixie_tube.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <vector>

enum class LedEffectCategory
{
    kBacklight,
    kBeats,
};

class LedEffect
{
public:
    LedEffect(const char *name, ILedStrip &strip);
    virtual ~LedEffect();

    void set_enabled(bool enabled);
    bool is_enabled() const;

    void set_targets(const std::vector<NixieTube *> &digits);
    void set_base_backlight(const BackLightState &back_light);
    BackLightState base_backlight() const;

    void tick(uint32_t dt_ms);
    const char *name() const;

protected:
    virtual void run(uint32_t dt_ms) = 0;
    std::vector<NixieTube *> targets_copy() const;
    void apply_backlight_to_targets(const BackLightState &back_light);
    ILedStrip &strip();
    SemaphoreHandle_t mutex_handle() const;

private:
    const char *name_;
    ILedStrip &strip_;
    std::vector<NixieTube *> targets_;
    BackLightState back_light_;
    bool enabled_;
    mutable SemaphoreHandle_t mutex_;
};

class BreathEffect : public LedEffect
{
public:
    BreathEffect(const char *name, ILedStrip &strip);

    void set_speed(float speed_hz);
    void set_max_brightness(uint8_t max_brightness);

protected:
    void run(uint32_t dt_ms) override;

private:
    float speed_hz_;
    float phase_;
    uint8_t max_brightness_;
};

class RainbowEffect : public LedEffect
{
public:
    RainbowEffect(const char *name, ILedStrip &strip);

    void set_speed(float degrees_per_second);

protected:
    void run(uint32_t dt_ms) override;

private:
    float speed_deg_per_sec_;
    float hue_;
};

class LedBeatsEffect : public LedEffect
{
public:
    LedBeatsEffect(const char *name, ILedStrip &strip);
};

class LedService
{
public:
    explicit LedService(ILedStrip &strip, TickType_t frame_delay_ticks = pdMS_TO_TICKS(10));
    ~LedService();

    void start();
    void stop();

    void set_effect(LedEffectCategory category, LedEffect *effect);
    void enable_category(LedEffectCategory category, bool enabled);
    void set_frame_delay(TickType_t frame_delay_ticks);

private:
    static void task_entry(void *param);
    void loop();

    ILedStrip &strip_;
    LedEffect *backlight_effect_;
    LedEffect *beats_effect_;
    bool backlight_enabled_;
    bool beats_enabled_;
    TickType_t frame_delay_ticks_;
    TaskHandle_t task_handle_;
    mutable SemaphoreHandle_t mutex_;
};
