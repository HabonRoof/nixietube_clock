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
      current_nixie_transition_(NixieTransitionType::INSTANT),
      effect_color_phase_(0.0f),
      effect_speed_(0.35f),
      base_backlight_{{0, 255, 255}, 255},
      base_nixie_brightness_(255),
      last_digits_valid_(false),
      divergence_{},
      date_elapsed_ms_(0),
      cathode_{},
      pomodoro_{},
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

void DisplayDaemon::reset_tube_transitions()
{
    for (size_t i = 0; i < tube_transitions_.size(); ++i) {
        tube_transitions_[i] = TubeTransitionState{};
        nixie_driver_.set_tube_brightness(i, 255);
    }
}

void DisplayDaemon::auto_return_clock()
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
    reset_tube_transitions();
    divergence_.phase = DivergencePhase::JUMPING;
    divergence_.elapsed_ms = 0;
    divergence_.final_value = esp_random() % 200000;
    nixie_driver_.display_number(esp_random() % 1000000);
}

void DisplayDaemon::reset_cathode_poisoning()
{
    reset_tube_transitions();
    cathode_.start_digit = static_cast<uint8_t>(esp_random() % 10);
    cathode_.step = 0;
    cathode_.step_elapsed_ms = 0;

    const std::array<uint8_t, 6> digits = {
        cathode_.start_digit, cathode_.start_digit, cathode_.start_digit,
        cathode_.start_digit, cathode_.start_digit, cathode_.start_digit,
    };
    nixie_driver_.set_digits(digits);
}

void DisplayDaemon::reset_pomodoro()
{
    reset_tube_transitions();
    pomodoro_.phase = PomodoroPhase::IDLE;
    pomodoro_.remaining_ms = kPomodoroWorkMs;
    pomodoro_.last_displayed_s = UINT32_MAX;
    render_pomodoro_display();
    apply_pomodoro_backlight(PomodoroPhase::IDLE);
}

void DisplayDaemon::start_pomodoro_work()
{
    pomodoro_.phase = PomodoroPhase::WORK;
    pomodoro_.remaining_ms = kPomodoroWorkMs;
    pomodoro_.last_displayed_s = UINT32_MAX;
    render_pomodoro_display();
    apply_pomodoro_backlight(PomodoroPhase::WORK);
}

void DisplayDaemon::start_pomodoro_break()
{
    pomodoro_.phase = PomodoroPhase::BREAK;
    pomodoro_.remaining_ms = kPomodoroBreakMs;
    pomodoro_.last_displayed_s = UINT32_MAX;
    render_pomodoro_display();
    apply_pomodoro_backlight(PomodoroPhase::BREAK);
}

void DisplayDaemon::apply_pomodoro_backlight(PomodoroPhase phase)
{
    const RgbColor rgb = (phase == PomodoroPhase::BREAK)
        ? RgbColor{0, 255, 0}
        : RgbColor{255, 140, 0};
    base_backlight_.color = rgb_to_hsv(rgb);

    if (phase == PomodoroPhase::IDLE) {
        current_effect_type_ = LedEffectType::NONE;
    } else {
        current_effect_type_ = LedEffectType::BREATH;
        effect_speed_ = 0.175f;
    }
    effect_color_phase_ = 0.0f;
}

void DisplayDaemon::render_pomodoro_display()
{
    const uint32_t total_s = pomodoro_.remaining_ms / 1000;
    if (total_s == pomodoro_.last_displayed_s) {
        return;
    }
    pomodoro_.last_displayed_s = total_s;
    handle_digit_update(digits_from_pomodoro());
}

void DisplayDaemon::update_pomodoro(uint32_t dt_ms)
{
    if (pomodoro_.phase == PomodoroPhase::IDLE) {
        return;
    }

    if (dt_ms >= pomodoro_.remaining_ms) {
        pomodoro_.remaining_ms = 0;
    } else {
        pomodoro_.remaining_ms -= dt_ms;
    }

    render_pomodoro_display();

    if (pomodoro_.remaining_ms > 0) {
        // If time is not up, continue the current phase
        return;
    }

    // If time is up, start the next phase
    if (pomodoro_.phase == PomodoroPhase::WORK) {
        start_pomodoro_break();
    } else if (pomodoro_.phase == PomodoroPhase::BREAK) {
        start_pomodoro_work();
    }
}

std::array<uint8_t, 6> DisplayDaemon::digits_from_time(const DisplayMessage &msg) const
{
    if (current_mode_ == DisplayMode::DATE_YYMMDD) {
        return {
            static_cast<uint8_t>(msg.data.time.yy / 10),
            static_cast<uint8_t>(msg.data.time.yy % 10),
            static_cast<uint8_t>(msg.data.time.mm / 10),
            static_cast<uint8_t>(msg.data.time.mm % 10),
            static_cast<uint8_t>(msg.data.time.dd / 10),
            static_cast<uint8_t>(msg.data.time.dd % 10),
        };
    }

    return {
        static_cast<uint8_t>(msg.data.time.h / 10),
        static_cast<uint8_t>(msg.data.time.h % 10),
        static_cast<uint8_t>(msg.data.time.m / 10),
        static_cast<uint8_t>(msg.data.time.m % 10),
        static_cast<uint8_t>(msg.data.time.s / 10),
        static_cast<uint8_t>(msg.data.time.s % 10),
    };
}

void DisplayDaemon::handle_digit_update(const std::array<uint8_t, 6> &digits)
{
    if (current_nixie_transition_ != NixieTransitionType::FADE || !last_digits_valid_) {
        nixie_driver_.set_digits(digits);
        last_digits_ = digits;
        last_digits_valid_ = true;
        return;
    }

    for (size_t i = 0; i < digits.size(); ++i) {
        if (digits[i] == last_digits_[i]) {
            continue;
        }

        TubeTransitionState &tube = tube_transitions_[i];
        tube.pending_digit = digits[i];

        if (tube.phase == TubeTransitionPhase::STABLE) {
            tube.phase = TubeTransitionPhase::FADE_OUT;
        }
    }

    last_digits_ = digits;
    last_digits_valid_ = true;
}

void DisplayDaemon::handle_time_update(const DisplayMessage &msg)
{
    handle_digit_update(digits_from_time(msg));
}

std::array<uint8_t, 6> DisplayDaemon::digits_from_pomodoro() const
{
    const uint32_t total_s = pomodoro_.remaining_ms / 1000;
    const uint8_t h = static_cast<uint8_t>(total_s / 3600);
    const uint8_t m = static_cast<uint8_t>((total_s % 3600) / 60);
    const uint8_t s = static_cast<uint8_t>(total_s % 60);

    return {
        static_cast<uint8_t>(h / 10),
        static_cast<uint8_t>(h % 10),
        static_cast<uint8_t>(m / 10),
        static_cast<uint8_t>(m % 10),
        static_cast<uint8_t>(s / 10),
        static_cast<uint8_t>(s % 10),
    };
}

void DisplayDaemon::update_nixie_transitions(uint32_t dt_ms)
{
    if (current_nixie_transition_ != NixieTransitionType::FADE) {
        return;
    }
    if (current_mode_ != DisplayMode::CLOCK_HHMMSS &&
        current_mode_ != DisplayMode::DATE_YYMMDD &&
        current_mode_ != DisplayMode::POMODORO) {
        return;
    }

    for (size_t i = 0; i < tube_transitions_.size(); ++i) {
        TubeTransitionState &tube = tube_transitions_[i];

        switch (tube.phase) {
            case TubeTransitionPhase::STABLE:
                break;

            case TubeTransitionPhase::FADE_OUT: {
                if (tube.current_scale <= kFadeStep) {
                    tube.current_scale = 0;
                    nixie_driver_.set_tube_brightness(i, 0);
                    nixie_driver_.set_digit_at(i, tube.pending_digit);
                    tube.phase = TubeTransitionPhase::FADE_IN;
                } else {
                    tube.current_scale = static_cast<uint8_t>(tube.current_scale - kFadeStep);
                    nixie_driver_.set_tube_brightness(i, tube.current_scale);
                }
                break;
            }

            case TubeTransitionPhase::FADE_IN: {
                if (tube.current_scale >= static_cast<uint8_t>(255 - kFadeStep)) {
                    tube.current_scale = 255;
                    nixie_driver_.set_tube_brightness(i, 255);
                    tube.phase = TubeTransitionPhase::STABLE;
                } else {
                    tube.current_scale = static_cast<uint8_t>(tube.current_scale + kFadeStep);
                    nixie_driver_.set_tube_brightness(i, tube.current_scale);
                }
                break;
            }
        }

        (void)dt_ms;
    }
}

void DisplayDaemon::update_divergence_meter(uint32_t dt_ms)
{
    divergence_.elapsed_ms += dt_ms;

    if (divergence_.elapsed_ms >= kAutoReturnDisplayMs) {
        auto_return_clock();
        return;
    }

    if (divergence_.phase == DivergencePhase::JUMPING) {
        const std::array<uint8_t, 6> temp_digits = {
            static_cast<uint8_t>(esp_random() % 10),
            static_cast<uint8_t>(esp_random() % 10),
            static_cast<uint8_t>(esp_random() % 10),
            static_cast<uint8_t>(esp_random() % 10),
            static_cast<uint8_t>(esp_random() % 10),
            static_cast<uint8_t>(esp_random() % 10),
        };
        nixie_driver_.set_digits(temp_digits);

        if (divergence_.elapsed_ms >= kDivergenceJumpMs) {
            divergence_.phase = DivergencePhase::FROZEN;
            nixie_driver_.display_number(divergence_.final_value);
        }
        return;
    }

    // FROZEN: hold final_value until auto-return.
}

void DisplayDaemon::update_date_display(uint32_t dt_ms)
{
    date_elapsed_ms_ += dt_ms;
    if (date_elapsed_ms_ >= kAutoReturnDisplayMs) {
        auto_return_clock();
    }
}

void DisplayDaemon::update_cathode_poisoning(uint32_t dt_ms)
{
    cathode_.step_elapsed_ms += dt_ms;
    if (cathode_.step_elapsed_ms < kCathodeStepMs) {
        return;
    }

    cathode_.step_elapsed_ms = 0;
    cathode_.step++;

    if (cathode_.step >= kCathodePoisonCtr) {
        auto_return_clock();
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
        } else if (current_mode_ == DisplayMode::POMODORO) {
            update_pomodoro(20);
            update_nixie_transitions(20);
        } else if (current_mode_ == DisplayMode::DATE_YYMMDD) {
            update_date_display(20);
            update_nixie_transitions(20);
        } else if (current_mode_ == DisplayMode::CLOCK_HHMMSS) {
            update_nixie_transitions(20);
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
            if (current_mode_ == DisplayMode::OFF) {
                break;
            }
            if (current_mode_ == DisplayMode::CLOCK_HHMMSS ||
                current_mode_ == DisplayMode::DATE_YYMMDD) {
                handle_time_update(msg);
            }
            break;
        case DisplayCmd::SET_MODE:
            current_mode_ = msg.data.mode;
            if (current_mode_ == DisplayMode::CLOCK_HHMMSS) {
                auto_return_requested_ = false;
                reset_tube_transitions();
                last_digits_valid_ = false;
            } else if (current_mode_ == DisplayMode::DATE_YYMMDD) {
                auto_return_requested_ = false;
                date_elapsed_ms_ = 0;
                reset_tube_transitions();
                last_digits_valid_ = false;
            }
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                reset_tube_transitions();
                nixie_driver_.display_number(manual_number_);
            } else if (current_mode_ == DisplayMode::DIVERGENCE_METER) {
                auto_return_requested_ = false;
                reset_divergence_meter();
            } else if (current_mode_ == DisplayMode::CATHODE_POISONING) {
                auto_return_requested_ = false;
                reset_cathode_poisoning();
            } else if (current_mode_ == DisplayMode::POMODORO) {
                last_digits_valid_ = false;
                reset_pomodoro();
            } else if (current_mode_ == DisplayMode::OFF) {
                reset_tube_transitions();
                nixie_driver_.set_brightness(0);
            }
            break;
        case DisplayCmd::DIVERGENCE_RESTART:
            if (current_mode_ == DisplayMode::DIVERGENCE_METER) {
                auto_return_requested_ = false;
                reset_divergence_meter();
            }
            break;
        case DisplayCmd::POMODORO_START:
            if (current_mode_ == DisplayMode::POMODORO &&
                pomodoro_.phase == PomodoroPhase::IDLE) {
                start_pomodoro_work();
            }
            break;
        case DisplayCmd::SET_MANUAL_NUMBER:
            manual_number_ = msg.data.number;
            if (current_mode_ == DisplayMode::MANUAL_DISPLAY) {
                reset_tube_transitions();
                nixie_driver_.display_number(manual_number_);
            }
            break;
        case DisplayCmd::SET_NIXIE_BRIGHTNESS:
            base_nixie_brightness_ = msg.data.brightness;
            nixie_driver_.set_brightness(msg.data.brightness);
            break;
        case DisplayCmd::SET_NIXIE_TRANSITION:
            current_nixie_transition_ =
                msg.data.transition_id == 1 ? NixieTransitionType::FADE : NixieTransitionType::INSTANT;
            if (current_nixie_transition_ == NixieTransitionType::INSTANT) {
                reset_tube_transitions();
            }
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
