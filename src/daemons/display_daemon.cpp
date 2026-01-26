#include "daemons/display_daemon.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "DisplayDaemon";
static constexpr float kTwoPi = 6.28318530718f;
static constexpr size_t kLedsPerTube = 4;
static constexpr size_t kTubeCount = 6;

DisplayDaemon::DisplayDaemon(INixieDriver &nixie_driver, ILedDriver &led_driver)
    : nixie_driver_(nixie_driver),
      led_driver_(led_driver),
      queue_(nullptr),
      task_handle_(nullptr),
      current_mode_(DisplayMode::CLOCK_HHMMSS),
      manual_number_(0),
      current_effect_type_(LedEffectType::BREATH),
      effect_color_phase_(0.0f),
      effect_speed_(0.35f),
      effect_max_brightness_(255),
      base_backlight_{{0, 255, 255}, 255} // Default Cyan
{
    queue_ = xQueueCreate(10, sizeof(DisplayMessage));
}

DisplayDaemon::~DisplayDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (queue_) {
        vQueueDelete(queue_);
    }
}

void DisplayDaemon::start()
{
    xTaskCreate(task_entry, "display_daemon", 4096, this, 5, &task_handle_);
}

QueueHandle_t DisplayDaemon::get_queue() const
{
    return queue_;
}

void DisplayDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<DisplayDaemon *>(param);
    daemon->loop();
}

void DisplayDaemon::loop()
{
    ESP_LOGI(TAG, "Display Daemon Started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frame_delay = pdMS_TO_TICKS(20); // 50Hz refresh rate

    while (true) {
        DisplayMessage msg;
        // Non-blocking check for messages
        while (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
            process_message(msg);
        }

        // Update Effects
        update_effects(20); // 20ms dt

        // Refresh Hardware
        led_driver_.show();
        
        vTaskDelayUntil(&last_wake_time, frame_delay);
    }
}

void DisplayDaemon::process_message(const DisplayMessage &msg)
{
    switch (msg.command) {
        case DisplayCmd::UPDATE_TIME:
            if (current_mode_ == DisplayMode::CLOCK_HHMMSS) {
                nixie_driver_.display_time(msg.data.time.h, msg.data.time.m, msg.data.time.s);
            }
            break;
        case DisplayCmd::SET_MODE:
            current_mode_ = msg.data.mode;
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                nixie_driver_.display_number(manual_number_);
            }
            break;
        case DisplayCmd::SET_MANUAL_NUMBER:
            manual_number_ = msg.data.number;
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                nixie_driver_.display_number(manual_number_);
            }
            break;
        case DisplayCmd::SET_BACKLIGHT_COLOR:
            base_backlight_.color.hue = 0; // Reset hue if using RGB, but we use HSV internally
            // Convert RGB to HSV or just use what we have.
            // The message structure has RGB, but the CLI sends HSV.
            // Let's assume the message structure was updated or we interpret it.
            // Wait, message_types.h has RGB in union.
            // I should have updated message_types.h to support HSV or convert here.
            // Let's stick to RGB in message for now and convert.
            {
                RgbColor rgb = {msg.data.color.r, msg.data.color.g, msg.data.color.b};
                base_backlight_.color = rgb_to_hsv(rgb);
            }
            break;
        case DisplayCmd::SET_BACKLIGHT_BRIGHTNESS:
            base_backlight_.brightness = msg.data.brightness;
            break;
        case DisplayCmd::SET_EFFECT:
            if (msg.data.effect_id == 1) {
                current_effect_type_ = LedEffectType::BREATH;
                effect_speed_ = 0.35f;
            } else if (msg.data.effect_id == 2) {
                current_effect_type_ = LedEffectType::RAINBOW;
                effect_speed_ = 60.0f;
            } else {
                current_effect_type_ = LedEffectType::NONE;
            }
            effect_color_phase_ = 0.0f;
            break;
        case DisplayCmd::UPDATE_BATTERY:
            ESP_LOGI(TAG, "Battery Update: %d%%, %d mV, %d mA, SOH: %d%%",
                     msg.data.battery.soc, msg.data.battery.voltage_mv,
                     msg.data.battery.current_ma, msg.data.battery.soh);
            break;
        default:
            break;
    }
}

void DisplayDaemon::update_effects(uint32_t dt_ms)
{
    switch (current_effect_type_) {
        case LedEffectType::BREATH:
            run_breath_effect(dt_ms);
            break;
        case LedEffectType::RAINBOW:
            run_rainbow_effect(dt_ms);
            break;
        case LedEffectType::NONE:
        default:
            break;
    }
}

void DisplayDaemon::apply_backlight_to_all(const BackLightState &state)
{
    HsvColor adjusted = state.color;
    uint16_t scaled_value = static_cast<uint16_t>(adjusted.value) * state.brightness / 255;
    adjusted.value = static_cast<uint8_t>(std::min<uint16_t>(scaled_value, 255));
    
    RgbColor rgb = hsv_to_rgb(adjusted);
    rgb = apply_gamma(rgb);

    // Map tubes to LEDs: Tube i -> LEDs [i*2, i*2+1]
    for (size_t i = 0; i < kTubeCount; ++i) {
        for (size_t j = 0; j < kLedsPerTube; ++j) {
            size_t led_index = i * kLedsPerTube + j;
            if (led_index < led_driver_.get_led_count()) {
                led_driver_.set_pixel(led_index, rgb.red, rgb.green, rgb.blue);
            }
        }
    }
}

void DisplayDaemon::run_breath_effect(uint32_t dt_ms)
{
    effect_color_phase_ += effect_speed_ * static_cast<float>(dt_ms) * kTwoPi / 1000.0f;
    if (effect_color_phase_ > kTwoPi)
    {
        effect_color_phase_ = std::fmod(effect_color_phase_, kTwoPi);
    }

    float normalized = (std::sin(effect_color_phase_) + 1.0f) * 0.5f;
    
    BackLightState current_state = base_backlight_;
    current_state.brightness = static_cast<uint8_t>(std::round(normalized * effect_max_brightness_));

    apply_backlight_to_all(current_state);
}

void DisplayDaemon::run_rainbow_effect(uint32_t dt_ms)
{
    effect_color_phase_ += effect_speed_ * static_cast<float>(dt_ms) / 1000.0f;
    if (effect_color_phase_ >= 360.0f)
    {
        effect_color_phase_ = std::fmod(effect_color_phase_, 360.0f);
    }

    BackLightState current_state = base_backlight_;
    current_state.color.hue = static_cast<uint16_t>(effect_color_phase_) % 360;
    
    apply_backlight_to_all(current_state);
}