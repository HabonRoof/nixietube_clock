#include "daemons/input_daemon.h"
#include "system_controller.h"
#include "wifi_manager.h"
#include "message_types.h"
#include "button_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "InputDaemon";

static constexpr uint32_t kPollMs = 20;
static constexpr uint32_t kDebounceMs = 50;
static constexpr uint32_t kInterPressMs = 200;

InputDaemon::InputDaemon(SystemController &system_controller, WifiManager *wifi_manager)
    : system_controller_(system_controller),
      wifi_manager_(wifi_manager),
      system_queue_(nullptr),
      task_handle_(nullptr),
      debounce_{},
      ap_config_combo_{},
      tick_ms_(0)
{
    for (auto &state : debounce_) {
        state.stable_pressed = false;
        state.last_raw_pressed = false;
        state.change_ms = 0;
        state.last_fire_ms = 0;
    }
    ap_config_combo_.tracking = false;
    ap_config_combo_.action_fired = false;
    ap_config_combo_.press_start_ms = 0;
}

InputDaemon::~InputDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void InputDaemon::start()
{
    system_queue_ = system_controller_.get_queue();
    init_gpio();
    xTaskCreate(task_entry, "input_daemon", 3072, this, 5, &task_handle_);
}

void InputDaemon::init_gpio()
{
    uint64_t mask = 0;
    for (uint8_t i = 0; i < kButtonCount; ++i) {
        mask |= (1ULL << kButtonPins[i]);
    }

    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = mask;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "Button GPIO initialized (poll %u ms, AP combo %u ms)", kPollMs, kLongPressMs);
}

void InputDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<InputDaemon *>(param);
    daemon->loop();
}

bool InputDaemon::ap_combo_blocks_button(uint8_t button_id) const
{
    if (ap_config_combo_.tracking) {
        return true;
    }

    if (ap_config_combo_.action_fired &&
        (button_id == kButtonModeCycle || button_id == kButtonProfileCycle)) {
        return true;
    }

    return false;
}

void InputDaemon::clear_ap_combo_if_released()
{
    if (!ap_config_combo_.action_fired) {
        return;
    }

    if (!debounce_[kButtonModeCycle].stable_pressed &&
        !debounce_[kButtonProfileCycle].stable_pressed) {
        ap_config_combo_.action_fired = false;
    }
}

bool InputDaemon::process_profile_button(uint8_t button_id, bool raw_pressed, bool prev_stable)
{
    (void)button_id;
    if (ap_combo_blocks_button(kButtonProfileCycle)) {
        return false;
    }

    if (!raw_pressed && prev_stable) {
        return true;
    }

    return false;
}

void InputDaemon::process_ap_config_combo(bool mode_pressed, bool profile_pressed)
{
    const bool both_pressed = mode_pressed && profile_pressed;

    if (!both_pressed) {
        ap_config_combo_.tracking = false;
        return;
    }

    if (!ap_config_combo_.tracking) {
        ap_config_combo_.tracking = true;
        ap_config_combo_.action_fired = false;
        ap_config_combo_.press_start_ms = tick_ms_;
        return;
    }

    if (ap_config_combo_.action_fired) {
        return;
    }

    if ((tick_ms_ - ap_config_combo_.press_start_ms) < kLongPressMs) {
        return;
    }

    ap_config_combo_.action_fired = true;
    if (!wifi_manager_) {
        return;
    }

    if (wifi_manager_->is_config_active()) {
        wifi_manager_->stop_config_ap();
        ESP_LOGI(TAG, "BTN_%u+BTN_%u long press: exit WiFi config AP", kButtonModeCycle,
                 kButtonProfileCycle);
    } else {
        wifi_manager_->enter_config_mode();
        ESP_LOGI(TAG, "BTN_%u+BTN_%u long press: enter WiFi config AP", kButtonModeCycle,
                 kButtonProfileCycle);
    }
}

void InputDaemon::loop()
{
    ESP_LOGI(TAG, "Input Daemon Started");

    while (true) {
        tick_ms_ += kPollMs;

        bool prev_stable[kButtonCount] = {};
        for (uint8_t i = 0; i < kButtonCount; ++i) {
            const bool raw_pressed = gpio_get_level(kButtonPins[i]) == 0;
            DebounceState &state = debounce_[i];

            if (raw_pressed != state.last_raw_pressed) {
                state.last_raw_pressed = raw_pressed;
                state.change_ms = tick_ms_;
            }

            prev_stable[i] = state.stable_pressed;
            if (raw_pressed != state.stable_pressed && (tick_ms_ - state.change_ms) >= kDebounceMs) {
                state.stable_pressed = raw_pressed;
            }
        }

        process_ap_config_combo(debounce_[kButtonModeCycle].stable_pressed,
                                debounce_[kButtonProfileCycle].stable_pressed);

        for (uint8_t i = 0; i < kButtonCount; ++i) {
            DebounceState &state = debounce_[i];

            if (i == kButtonProfileCycle) {
                if (process_profile_button(i, state.stable_pressed, prev_stable[i])) {
                    if ((tick_ms_ - state.last_fire_ms) >= kInterPressMs) {
                        state.last_fire_ms = tick_ms_;
                        post_button_pressed(i);
                    }
                }
                continue;
            }

            if (ap_combo_blocks_button(i)) {
                continue;
            }

            if (state.stable_pressed && !prev_stable[i]) {
                if ((tick_ms_ - state.last_fire_ms) >= kInterPressMs) {
                    state.last_fire_ms = tick_ms_;
                    post_button_pressed(i);
                }
            }
        }

        clear_ap_combo_if_released();
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

void InputDaemon::post_button_pressed(uint8_t button_id)
{
    if (!system_queue_) {
        return;
    }

    SystemMessage msg = {};
    msg.event = SystemEvent::BUTTON_PRESSED;
    msg.data.button_id = button_id;
    xQueueSend(system_queue_, &msg, 0);
    ESP_LOGI(TAG, "Button %u pressed", button_id);
}
