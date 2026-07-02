#include "daemons/input_daemon.h"
#include "system_controller.h"
#include "message_types.h"
#include "button_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "InputDaemon";

static constexpr uint32_t kPollMs = 20;
static constexpr uint32_t kDebounceMs = 50;
static constexpr uint32_t kInterPressMs = 200;

InputDaemon::InputDaemon(SystemController &system_controller)
    : system_controller_(system_controller),
      system_queue_(nullptr),
      task_handle_(nullptr),
      debounce_{},
      tick_ms_(0)
{
    for (auto &state : debounce_) {
        state.stable_pressed = false;
        state.last_raw_pressed = false;
        state.change_ms = 0;
        state.last_fire_ms = 0;
    }
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
    ESP_LOGI(TAG, "Button GPIO initialized (poll %u ms)", kPollMs);
}

void InputDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<InputDaemon *>(param);
    daemon->loop();
}

void InputDaemon::loop()
{
    ESP_LOGI(TAG, "Input Daemon Started");

    while (true) {
        tick_ms_ += kPollMs;

        for (uint8_t i = 0; i < kButtonCount; ++i) {
            const bool raw_pressed = gpio_get_level(kButtonPins[i]) == 0;
            if (process_debounce(i, raw_pressed, tick_ms_)) {
                post_button_pressed(i);
            }
        }

        poll_als();
        vTaskDelay(pdMS_TO_TICKS(kPollMs));
    }
}

bool InputDaemon::process_debounce(uint8_t button_id, bool raw_pressed, uint32_t now_ms)
{
    DebounceState &state = debounce_[button_id];

    if (raw_pressed != state.last_raw_pressed) {
        state.last_raw_pressed = raw_pressed;
        state.change_ms = now_ms;
    }

    const bool prev_stable = state.stable_pressed;
    if (raw_pressed != state.stable_pressed &&
        (now_ms - state.change_ms) >= kDebounceMs) {
        state.stable_pressed = raw_pressed;
    }

    if (state.stable_pressed && !prev_stable) {
        if ((now_ms - state.last_fire_ms) >= kInterPressMs) {
            state.last_fire_ms = now_ms;
            return true;
        }
    }

    return false;
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

void InputDaemon::poll_als()
{
    // Stub for future LTR-303ALS-01 integration.
}
