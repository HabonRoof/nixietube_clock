#pragma once

#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "message_types.h"
#include "nixie_driver.h"
#include "led_driver.h"
#include "system_state.h"

enum class LedEffectType
{
    NONE,
    BREATH,
    RAINBOW,
    OFF
};

enum class NixieTransitionType
{
    INSTANT,
    FADE,
};

enum class TubeTransitionPhase
{
    STABLE,
    FADE_OUT,
    FADE_IN,
};

enum class DivergencePhase
{
    JUMPING,
    FROZEN,
};

struct TubeTransitionState
{
    TubeTransitionPhase phase = TubeTransitionPhase::STABLE;
    uint8_t pending_digit = 0;
    uint8_t current_scale = 255;
};

struct DivergenceMeterState
{
    DivergencePhase phase = DivergencePhase::JUMPING;
    uint32_t elapsed_ms = 0;
    uint32_t final_value = 0;
};

struct CathodePoisoningState
{
    uint8_t start_digit = 0;
    uint8_t step = 0;
    uint32_t step_elapsed_ms = 0;
};

enum class PomodoroPhase
{
    IDLE,
    WORK,
    BREAK,
};

struct PomodoroState
{
    PomodoroPhase phase = PomodoroPhase::IDLE;
    uint32_t remaining_ms = 0;
    uint32_t last_displayed_s = UINT32_MAX;
};

class DisplayDaemon
{
public:
    DisplayDaemon(INixieDriver &nixie_driver, ILedDriver &led_driver, SystemState &system_state);
    ~DisplayDaemon();

    void start();
    void set_system_queue(QueueHandle_t queue);
    QueueHandle_t get_queue() const;

private:
    static void task_entry(void *param);
    void loop();
    void process_message(const DisplayMessage &msg);
    void update_effects(uint32_t dt_ms);
    void update_divergence_meter(uint32_t dt_ms);
    void update_date_display(uint32_t dt_ms);
    void update_cathode_poisoning(uint32_t dt_ms);
    void update_pomodoro(uint32_t dt_ms);
    void update_nixie_transitions(uint32_t dt_ms);
    void reset_divergence_meter();
    void reset_cathode_poisoning();
    void reset_pomodoro();
    void start_pomodoro_work();
    void start_pomodoro_break();
    void apply_pomodoro_backlight(PomodoroPhase phase);
    void render_pomodoro_display();
    void reset_tube_transitions();
    void auto_return_clock();
    void handle_time_update(const DisplayMessage &msg);
    void handle_digit_update(const std::array<uint8_t, 6> &digits);
    std::array<uint8_t, 6> digits_from_time(const DisplayMessage &msg) const;
    std::array<uint8_t, 6> digits_from_pomodoro() const;

    void run_breath_effect(uint32_t dt_ms);
    void run_rainbow_effect(uint32_t dt_ms);
    void turn_off_backlight();
    void apply_backlight_to_all(const BackLightState &state);

    static constexpr uint32_t kAutoReturnDisplayMs = 10000;
    static constexpr uint32_t kDivergenceJumpMs = 3000;
    static constexpr uint32_t kCathodeStepMs = 300;
    static constexpr uint8_t kCathodePoisonCtr = 15;
    static constexpr uint8_t kFadeStep = 51;
    static constexpr uint32_t kPomodoroWorkMs = 25 * 60 * 1000;
    static constexpr uint32_t kPomodoroBreakMs = 1 * 60 * 1000;

    INixieDriver &nixie_driver_;
    ILedDriver &led_driver_;
    SystemState &system_state_;
    QueueHandle_t queue_;
    QueueHandle_t system_queue_;
    TaskHandle_t task_handle_;

    DisplayMode current_mode_;
    uint32_t manual_number_;
    LedEffectType current_effect_type_;
    NixieTransitionType current_nixie_transition_;
    float effect_color_phase_;
    float effect_speed_;
    BackLightState base_backlight_;
    uint8_t base_nixie_brightness_;
    std::array<uint8_t, 6> last_digits_{};
    bool last_digits_valid_;
    std::array<TubeTransitionState, 6> tube_transitions_{};
    DivergenceMeterState divergence_;
    uint32_t date_elapsed_ms_;
    CathodePoisoningState cathode_;
    PomodoroState pomodoro_;
    bool auto_return_requested_;
};
