#include "daemons/als_daemon.h"
#include "message_types.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "AlsDaemon";

AlsDaemon::AlsDaemon(Ltr303 &sensor, DisplayDaemon &display_daemon, INixieDriver &nixie_driver,
                       SystemState &system_state)
    : sensor_(sensor),
      display_daemon_(display_daemon),
      nixie_driver_(nixie_driver),
      system_state_(system_state),
      task_handle_(nullptr),
      smoothed_lux_(0.0f),
      has_smoothed_lux_(false),
      auto_brightness_active_(false),
      log_counter_(0)
{
}

AlsDaemon::~AlsDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void AlsDaemon::start()
{
    xTaskCreate(task_entry, "als_daemon", 4096, this, 4, &task_handle_);
}

void AlsDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<AlsDaemon *>(param);
    daemon->loop();
}

float AlsDaemon::lux_to_scale(float lux) const
{
    const float clamped_lux = std::clamp(lux, kLuxMin, kLuxMax);
    const float log_den = std::log(kLuxMax + 1.0f);
    if (log_den <= 0.0f) {
        return 1.0f;
    }

    float scale = std::log(clamped_lux + 1.0f) / log_den;
    return std::clamp(scale, kMinScale, 1.0f);
}

void AlsDaemon::loop()
{
    ESP_LOGI(TAG, "ALS Daemon Started");

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        ClockSettings settings{};
        system_state_.get_settings(&settings);

        const bool should_auto = sensor_.is_ready() && settings.auto_brightness_enabled;

        if (should_auto) {
            auto_brightness_active_ = true;
            float raw_lux = 0.0f;
            if (sensor_.read_raw_lux(&raw_lux)) {
                if (!has_smoothed_lux_) {
                    smoothed_lux_ = raw_lux;
                    has_smoothed_lux_ = true;
                } else {
                    smoothed_lux_ = kEmaAlpha * raw_lux + (1.0f - kEmaAlpha) * smoothed_lux_;
                }

                const float scale = lux_to_scale(smoothed_lux_);
                const uint8_t scale_byte = static_cast<uint8_t>(std::round(scale * 255.0f));
                const uint8_t nixie_target = static_cast<uint8_t>(
                    (static_cast<uint16_t>(settings.nixie_brightness) * scale_byte) / 255);

                DisplayMessage dmsg{};
                dmsg.command = DisplayCmd::SET_AMBIENT_SCALE;
                dmsg.data.brightness = scale_byte;
                xQueueSend(display_daemon_.get_queue(), &dmsg, 0);

                nixie_driver_.fade_brightness(nixie_target, kNixieFadeMs);

                AmbientLightStatus ambient{};
                ambient.lux = smoothed_lux_;
                ambient.scale = scale_byte;
                ambient.valid = true;
                system_state_.update_ambient(ambient);

                if (++log_counter_ >= kLogIntervalTicks) {
                    log_counter_ = 0;
                    ESP_LOGI(TAG, "lux=%.1f scale=%u nixie=%u", smoothed_lux_, scale_byte,
                             nixie_target);
                }
            }
        } else if (auto_brightness_active_) {
            auto_brightness_active_ = false;
            has_smoothed_lux_ = false;

            DisplayMessage dmsg{};
            dmsg.command = DisplayCmd::SET_AMBIENT_SCALE;
            dmsg.data.brightness = 255;
            xQueueSend(display_daemon_.get_queue(), &dmsg, 0);
        }

        vTaskDelayUntil(&last_wake_time, kPeriodTicks);
    }
}
