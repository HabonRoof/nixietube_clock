#include "daemons/display_daemon.h"
#include "esp_log.h"
#include "esp_random.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "DisplayDaemon";
static constexpr float kTwoPi = 6.28318530718f;

DisplayDaemon::DisplayDaemon(INixieDriver &nixie_driver, ILedDriver &led_driver, SystemState &system_state)
    : nixie_driver_(nixie_driver),
      led_driver_(led_driver),
      system_state_(system_state),
      queue_(nullptr),
      system_queue_(nullptr),
      task_handle_(nullptr),
      current_mode_(DisplayMode::CLOCK_HHMMSS),
      manual_number_(0),
      current_effect_type_(LedEffectType::BREATH),
      effect_color_phase_(0.0f),
      effect_speed_(0.35f),
      base_backlight_{{0, 255, 255}, 255},
      divergence_{},
      cathode_{},
      auto_return_requested_(false)
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

void DisplayDaemon::set_system_queue(QueueHandle_t queue)
{
    system_queue_ = queue;
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

void DisplayDaemon::request_auto_return_clock()
{
    if (auto_return_requested_ || !system_queue_) {
        return;
    }

    auto_return_requested_ = true;
    SystemMessage msg = {};
    msg.event = SystemEvent::AUTO_RETURN_CLOCK;
    xQueueSend(system_queue_, &msg, 0);
}

void DisplayDaemon::reset_divergence_meter()
{
    divergence_.phase = DivergencePhase::JUMPING;
    divergence_.elapsed_ms = 0;
    divergence_.since_jump_ms = 0;
    divergence_.final_value = esp_random() % 200000;
    nixie_driver_.display_number(esp_random() % 1000000);
}

void DisplayDaemon::reset_cathode_poisoning()
{
    cathode_.start_digit = static_cast<uint8_t>(esp_random() % 10);
    cathode_.step = 0;
    cathode_.step_elapsed_ms = 0;

    const std::array<uint8_t, 6> digits = {
        cathode_.start_digit, cathode_.start_digit, cathode_.start_digit,
        cathode_.start_digit, cathode_.start_digit, cathode_.start_digit,
    };
    nixie_driver_.set_digits(digits);
}

void DisplayDaemon::update_divergence_meter(uint32_t dt_ms)
{
    divergence_.elapsed_ms += dt_ms;

    if (divergence_.elapsed_ms >= kDivergenceTotalMs) {
        request_auto_return_clock();
        return;
    }

    if (divergence_.phase == DivergencePhase::JUMPING) {
        divergence_.since_jump_ms += dt_ms;
        if (divergence_.since_jump_ms >= kDivergenceStepMs) {
            divergence_.since_jump_ms = 0;
            nixie_driver_.display_number(esp_random() % 1000000);
        }

        if (divergence_.elapsed_ms >= kDivergenceJumpMs) {
            divergence_.phase = DivergencePhase::FROZEN;
            nixie_driver_.display_number(divergence_.final_value);
        }
        return;
    }

    // FROZEN: hold final_value until auto-return.
}

void DisplayDaemon::update_cathode_poisoning(uint32_t dt_ms)
{
    cathode_.step_elapsed_ms += dt_ms;
    if (cathode_.step_elapsed_ms < kCathodeStepMs) {
        return;
    }

    cathode_.step_elapsed_ms = 0;
    cathode_.step++;

    if (cathode_.step >= kCathodeSteps) {
        request_auto_return_clock();
        return;
    }

    const uint8_t digit = static_cast<uint8_t>((cathode_.start_digit + cathode_.step) % 10);
    const std::array<uint8_t, 6> digits = {digit, digit, digit, digit, digit, digit};
    nixie_driver_.set_digits(digits);
}

void DisplayDaemon::loop()
{
    ESP_LOGI(TAG, "Display Daemon Started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frame_delay = pdMS_TO_TICKS(20);

    while (true) {
        DisplayMessage msg;
        while (xQueueReceive(queue_, &msg, 0) == pdTRUE) {
            process_message(msg);
        }

        if (current_mode_ == DisplayMode::DIVERGENCE_METER) {
            update_divergence_meter(20);
        } else if (current_mode_ == DisplayMode::CATHODE_POISONING) {
            update_cathode_poisoning(20);
        }

        update_effects(20);
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
            } else if (current_mode_ == DisplayMode::DATE_YYMMDD) {
                nixie_driver_.display_date(msg.data.time.yy, msg.data.time.mm, msg.data.time.dd);
            }
            break;
        case DisplayCmd::SET_MODE:
            current_mode_ = msg.data.mode;
            if (current_mode_ == DisplayMode::CLOCK_HHMMSS ||
                current_mode_ == DisplayMode::DATE_YYMMDD) {
                auto_return_requested_ = false;
            }
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                nixie_driver_.display_number(manual_number_);
            } else             if (current_mode_ == DisplayMode::DIVERGENCE_METER) {
                auto_return_requested_ = false;
                reset_divergence_meter();
            } else if (current_mode_ == DisplayMode::CATHODE_POISONING) {
                auto_return_requested_ = false;
                reset_cathode_poisoning();
            }
            break;
        case DisplayCmd::DIVERGENCE_RESTART:
            if (current_mode_ == DisplayMode::DIVERGENCE_METER) {
                auto_return_requested_ = false;
                reset_divergence_meter();
            }
            break;
        case DisplayCmd::SET_MANUAL_NUMBER:
            manual_number_ = msg.data.number;
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                nixie_driver_.display_number(manual_number_);
            }
            break;
        case DisplayCmd::SET_NIXIE_BRIGHTNESS:
            nixie_driver_.set_brightness(msg.data.brightness);
            break;
        case DisplayCmd::SET_BACKLIGHT_COLOR:
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
            } else if (msg.data.effect_id == 3) {
                current_effect_type_ = LedEffectType::OFF;
            } else {
                current_effect_type_ = LedEffectType::NONE;
            }
            effect_color_phase_ = 0.0f;
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
            apply_backlight_to_all(base_backlight_);
            break;
        case LedEffectType::OFF:
            turn_off_backlight();
            break;
        default:
            break;
    }
}

void DisplayDaemon::turn_off_backlight()
{
    const size_t led_count = led_driver_.get_led_count();
    for (size_t led_index = 0; led_index < led_count; ++led_index) {
        led_driver_.set_pixel(led_index, 0, 0, 0);
    }
}

void DisplayDaemon::apply_backlight_to_all(const BackLightState &state)
{
    HsvColor adjusted = state.color;
    uint16_t scaled_value = static_cast<uint16_t>(adjusted.value) * state.brightness / 255;
    adjusted.value = static_cast<uint8_t>(std::min<uint16_t>(scaled_value, 255));

    RgbColor rgb = hsv_to_rgb(adjusted);
    rgb = apply_gamma(rgb);

    const size_t led_count = led_driver_.get_led_count();
    for (size_t led_index = 0; led_index < led_count; ++led_index) {
        led_driver_.set_pixel(led_index, rgb.red, rgb.green, rgb.blue);
    }
}

void DisplayDaemon::run_breath_effect(uint32_t dt_ms)
{
    effect_color_phase_ += effect_speed_ * static_cast<float>(dt_ms) * kTwoPi / 1000.0f;
    if (effect_color_phase_ > kTwoPi) {
        effect_color_phase_ = std::fmod(effect_color_phase_, kTwoPi);
    }

    float normalized = (std::sin(effect_color_phase_) + 1.0f) * 0.5f;

    BackLightState current_state = base_backlight_;
    current_state.brightness = static_cast<uint8_t>(std::round(normalized * base_backlight_.brightness));

    apply_backlight_to_all(current_state);
}

void DisplayDaemon::run_rainbow_effect(uint32_t dt_ms)
{
    effect_color_phase_ += effect_speed_ * static_cast<float>(dt_ms) / 1000.0f;
    if (effect_color_phase_ >= 360.0f) {
        effect_color_phase_ = std::fmod(effect_color_phase_, 360.0f);
    }

    BackLightState current_state = base_backlight_;
    current_state.color.hue = static_cast<uint16_t>(effect_color_phase_) % 360;

    apply_backlight_to_all(current_state);
}
