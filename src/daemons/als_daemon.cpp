#include "daemons/als_daemon.h"
#include "auto_brightness.h"
#include "message_types.h"
#include "esp_log.h"
#include <cmath>
#include <algorithm>

static const char *TAG = "AlsDaemon";

namespace {

constexpr Ltr303Gain kGainAutoSelectOrder[] = {
    Ltr303Gain::X96,
    Ltr303Gain::X48,
    Ltr303Gain::X8,
    Ltr303Gain::X4,
    Ltr303Gain::X2,
    Ltr303Gain::X1,
};

} // namespace

AlsDaemon::AlsDaemon(Ltr303 &sensor, DisplayDaemon &display_daemon, SystemState &system_state)
    : sensor_(sensor),
      display_daemon_(display_daemon),
      system_state_(system_state),
      task_handle_(nullptr),
      smoothed_lux_(0.0f),
      has_smoothed_lux_(false),
      auto_brightness_active_(false),
      applied_factor_(kAmbientFullScale),
      pending_factor_(kAmbientFullScale),
      last_factor_change_ms_(0),
      tick_ms_(0),
      log_counter_(0),
      cal_save_counter_(0),
      last_backlight_(0),
      last_nixie_(0)
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

void AlsDaemon::apply_stored_sensor_gain()
{
    AutoBrightnessCalibration cal{};
    system_state_.get_auto_brightness_calibration(&cal);
    const Ltr303Gain gain = ltr303_gain_from_storage(cal.als_gain);
    if (sensor_.is_ready()) {
        sensor_.set_gain(gain);
    }
}

void AlsDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<AlsDaemon *>(param);
    daemon->loop();
}

void AlsDaemon::reset_auto_brightness_state()
{
    has_smoothed_lux_ = false;
    applied_factor_ = kAmbientFullScale;
    pending_factor_ = kAmbientFullScale;
    last_factor_change_ms_ = 0;
}

void AlsDaemon::send_ambient_factor(uint16_t factor)
{
    DisplayMessage dmsg{};
    dmsg.command = DisplayCmd::SET_AMBIENT_SCALE;
    dmsg.data.ambient_factor = factor;
    push_display(dmsg);
}

void AlsDaemon::push_display(const DisplayMessage &msg)
{
    xQueueSend(display_daemon_.get_queue(), &msg, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
}

float AlsDaemon::compensate_ir(float lux, float ratio) const
{
    if (ratio > 0.6f) {
        return lux * 0.75f;
    }
    if (ratio > 0.3f) {
        return lux * 0.9f;
    }
    return lux;
}

AlsCalSample AlsDaemon::make_cal_sample(const Ltr303Sample &raw) const
{
    AlsCalSample sample{};
    sample.ch0 = raw.ch0;
    sample.ch1 = raw.ch1;
    sample.ratio = raw.ratio;
    sample.lux_raw = raw.lux;
    sample.lux = compensate_ir(raw.lux, raw.ratio);
    return sample;
}

bool AlsDaemon::measure_averaged(AlsCalSample *out, TickType_t period_ticks, TickType_t settle_ticks,
                                 int *sample_count_out)
{
    if (!out || period_ticks == 0) {
        return false;
    }

    vTaskDelay(settle_ticks);

    const TickType_t start = xTaskGetTickCount();
    AlsCalSample accum{};
    int count = 0;

    while ((xTaskGetTickCount() - start) < period_ticks) {
        Ltr303Sample raw{};
        vTaskDelay(kPeriodTicks);
        if (!sensor_.read_channels(&raw)) {
            continue;
        }
        const AlsCalSample sample = make_cal_sample(raw);
        accum.ch0 += sample.ch0;
        accum.ch1 += sample.ch1;
        accum.ratio += sample.ratio;
        accum.lux_raw += sample.lux_raw;
        accum.lux += sample.lux;
        ++count;
    }

    if (count == 0) {
        return false;
    }

    if (sample_count_out) {
        *sample_count_out = count;
    }

    const float inv = 1.0f / static_cast<float>(count);
    out->ch0 = static_cast<uint16_t>(std::round(static_cast<float>(accum.ch0) / count));
    out->ch1 = static_cast<uint16_t>(std::round(static_cast<float>(accum.ch1) / count));
    out->ratio = accum.ratio * inv;
    out->lux_raw = accum.lux_raw * inv;
    out->lux = accum.lux * inv;
    return true;
}

void AlsDaemon::apply_cal_hardware(uint8_t backlight, uint8_t nixie, bool white_led)
{
    DisplayMessage dmsg{};

    dmsg.command = DisplayCmd::SET_EFFECT;
    dmsg.data.effect_id = 0;
    push_display(dmsg);

    if (white_led) {
        dmsg.command = DisplayCmd::SET_BACKLIGHT_COLOR;
        dmsg.data.color.r = 255;
        dmsg.data.color.g = 255;
        dmsg.data.color.b = 255;
        push_display(dmsg);
    }

    dmsg.command = DisplayCmd::SET_BACKLIGHT_BRIGHTNESS;
    dmsg.data.brightness = backlight;
    push_display(dmsg);

    dmsg.command = DisplayCmd::SET_NIXIE_BRIGHTNESS;
    dmsg.data.brightness = nixie;
    push_display(dmsg);
}

bool AlsDaemon::select_calibration_gain(Ltr303Gain *selected_gain)
{
    if (!selected_gain) {
        return false;
    }

    apply_cal_hardware(255, 255, true);

    for (Ltr303Gain gain : kGainAutoSelectOrder) {
        if (!sensor_.set_gain(gain)) {
            continue;
        }

        Ltr303Sample probe{};
        vTaskDelay(kCalSettleTicks);
        if (!sensor_.read_channels(&probe)) {
            continue;
        }

        if (!Ltr303::is_saturated(probe)) {
            *selected_gain = gain;
            ESP_LOGI(TAG, "Self-cal selected gain %s", Ltr303::gain_label(gain));
            return true;
        }

        ESP_LOGW(TAG, "Self-cal gain %s saturated (ch0=%u ch1=%u)", Ltr303::gain_label(gain),
                 probe.ch0, probe.ch1);
    }

    ESP_LOGE(TAG, "Self-cal failed: ALS saturated at all gains; use a darker room");
    return false;
}

void AlsDaemon::log_cal_sample(const char *label, const AlsCalSample &sample, int sample_count) const
{
    ESP_LOGI(TAG,
             "Self-cal %s raw_avg (%d samples/10s): ch0=%u ch1=%u lux_raw=%.2f lux=%.2f ratio=%.3f",
             label, sample_count, sample.ch0, sample.ch1, sample.lux_raw, sample.lux, sample.ratio);
}

float AlsDaemon::subtract_self_light(float lux_measured, uint8_t backlight, uint8_t nixie,
                                     const AutoBrightnessCalibration &cal) const
{
    float lux = lux_measured;
    lux -= cal.k_led * static_cast<float>(backlight) / 255.0f;
    lux -= cal.k_nixie * static_cast<float>(nixie) / 255.0f;
    return std::max(lux, 0.01f);
}

void AlsDaemon::update_smoothed_lux(float lux_new, float dt_s)
{
    if (!has_smoothed_lux_) {
        smoothed_lux_ = lux_new;
        has_smoothed_lux_ = true;
        return;
    }

    const float tau = lux_new > smoothed_lux_ ? kTauBrightSec : kTauDarkSec;
    const float alpha = 1.0f - std::exp(-dt_s / tau);
    smoothed_lux_ = alpha * lux_new + (1.0f - alpha) * smoothed_lux_;
}

void AlsDaemon::clamp_calibration(AutoBrightnessCalibration *cal) const
{
    cal->lux_min = std::clamp(cal->lux_min, 0.01f, 9999.0f);
    cal->lux_max = std::clamp(cal->lux_max, cal->lux_min + 10.0f, 10000.0f);
    cal->k_led = std::max(cal->k_led, 0.0f);
    cal->k_nixie = std::max(cal->k_nixie, 0.0f);
}

void AlsDaemon::update_calibration_learning(AutoBrightnessCalibration *cal, float lux)
{
    if (cal->mode != AutoBrightnessCalMode::Auto) {
        return;
    }

    if (lux < cal->lux_min) {
        cal->lux_min += kLuxLearnMinAlpha * (lux - cal->lux_min);
    } else {
        cal->lux_min += kLuxLearnRelaxAlpha * (lux - cal->lux_min);
    }

    if (lux > cal->lux_max) {
        cal->lux_max += kLuxLearnMaxAlpha * (lux - cal->lux_max);
    } else {
        cal->lux_max += kLuxLearnRelaxAlpha * (lux - cal->lux_max);
    }

    clamp_calibration(cal);
}

uint16_t AlsDaemon::lux_to_factor(float lux, const AutoBrightnessCalibration &cal) const
{
    const float lux_min = cal.lux_min;
    const float lux_max = cal.lux_max;
    const float log_min = std::log(lux_min + 1.0f);
    const float log_max = std::log(lux_max + 1.0f);
    const float log_den = log_max - log_min;
    if (log_den <= 0.0f) {
        return kAmbientFullScale;
    }

    const float clamped_lux = std::clamp(lux, lux_min, lux_max);
    float normalized = (std::log(clamped_lux + 1.0f) - log_min) / log_den;
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    const float perceptual = std::pow(normalized, kGammaDark);
    const float span = static_cast<float>(kAmbientFullScale - kAmbientMinFactor);
    const float factor_f =
        static_cast<float>(kAmbientMinFactor) + perceptual * span;
    return static_cast<uint16_t>(std::clamp(std::round(factor_f),
                                            static_cast<float>(kAmbientMinFactor),
                                            static_cast<float>(kAmbientFullScale)));
}

uint16_t AlsDaemon::apply_slew_limit(uint16_t target_factor)
{
    if (target_factor > applied_factor_) {
        const uint16_t max_up = static_cast<uint16_t>(applied_factor_ + kMaxSlewPerStep);
        return std::min(target_factor, max_up);
    }
    if (target_factor < applied_factor_) {
        const uint16_t min_down =
            applied_factor_ > kMaxSlewPerStep ? applied_factor_ - kMaxSlewPerStep : 0;
        return std::max(target_factor, min_down);
    }
    return target_factor;
}

uint16_t AlsDaemon::apply_hysteresis(uint16_t target_factor)
{
    const uint16_t delta = target_factor > applied_factor_
                               ? target_factor - applied_factor_
                               : applied_factor_ - target_factor;
    const uint16_t threshold =
        target_factor > applied_factor_ ? kHystUp : kHystDown;

    if (delta < threshold) {
        return applied_factor_;
    }

    if ((tick_ms_ - last_factor_change_ms_) < kMinHoldMs && applied_factor_ != kAmbientFullScale) {
        return applied_factor_;
    }

    target_factor = apply_slew_limit(target_factor);
    applied_factor_ = target_factor;
    last_factor_change_ms_ = tick_ms_;
    return applied_factor_;
}

bool AlsDaemon::run_self_calibration()
{
    ESP_LOGI(TAG, "Self-calibration started (3-step: baseline, white LED, LED+nixie)");

    SavedDisplayState saved{};
    display_daemon_.capture_cal_snapshot(&saved.snapshot);
    saved.suppress_auto_brightness = system_state_.is_auto_brightness_suppressed();
    system_state_.set_auto_brightness_suppressed(true);
    send_ambient_factor(kAmbientFullScale);

    Ltr303Gain selected_gain = Ltr303Gain::X1;
    if (!select_calibration_gain(&selected_gain)) {
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }

    if (!sensor_.set_gain(selected_gain)) {
        ESP_LOGE(TAG, "Self-cal failed: could not apply selected gain");
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }

    AlsCalSample baseline{};
    int baseline_samples = 0;
    apply_cal_hardware(0, 0, false);
    if (!measure_averaged(&baseline, kCalSamplePeriodTicks, kCalSettleTicks, &baseline_samples)) {
        ESP_LOGW(TAG, "Self-cal failed: baseline read");
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }
    log_cal_sample("baseline", baseline, baseline_samples);

    AlsCalSample aggressor1{};
    int aggressor1_samples = 0;
    apply_cal_hardware(255, 0, true);
    if (!measure_averaged(&aggressor1, kCalSamplePeriodTicks, kCalSettleTicks, &aggressor1_samples)) {
        ESP_LOGW(TAG, "Self-cal failed: aggressor1 (white LED) read");
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }
    log_cal_sample("aggressor1", aggressor1, aggressor1_samples);

    AlsCalSample aggressor2{};
    int aggressor2_samples = 0;
    apply_cal_hardware(255, 255, true);
    if (!measure_averaged(&aggressor2, kCalSamplePeriodTicks, kCalSettleTicks, &aggressor2_samples)) {
        ESP_LOGW(TAG, "Self-cal failed: aggressor2 (LED+nixie) read");
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }
    if (Ltr303::is_saturated({aggressor2.ch0, aggressor2.ch1, aggressor2.ratio, aggressor2.lux_raw})) {
        ESP_LOGE(TAG, "Self-cal failed: aggressor2 saturated at gain %s",
                 Ltr303::gain_label(selected_gain));
        display_daemon_.restore_cal_snapshot(saved.snapshot);
        system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
        return false;
    }
    log_cal_sample("aggressor2", aggressor2, aggressor2_samples);

    AutoBrightnessCalibration cal{};
    system_state_.get_auto_brightness_calibration(&cal);
    cal.k_led = std::max(aggressor1.lux - baseline.lux, 0.0f);
    cal.k_nixie = std::max(aggressor2.lux - aggressor1.lux, 0.0f);
    cal.als_gain = ltr303_gain_to_storage(selected_gain);
    cal.version = kAutoBrightnessCalVersion;
    system_state_.set_auto_brightness_calibration(cal);
    system_state_.save_auto_brightness_calibration();

    ESP_LOGI(TAG, "Self-cal gain: %s", Ltr303::gain_label(selected_gain));
    ESP_LOGI(TAG, "Self-cal raw summary (10s average per step):");
    ESP_LOGI(TAG, "  baseline:   n=%d ch0=%u ch1=%u lux_raw=%.2f lux=%.2f ratio=%.3f",
             baseline_samples, baseline.ch0, baseline.ch1, baseline.lux_raw, baseline.lux,
             baseline.ratio);
    ESP_LOGI(TAG, "  aggressor1: n=%d ch0=%u ch1=%u lux_raw=%.2f lux=%.2f ratio=%.3f",
             aggressor1_samples, aggressor1.ch0, aggressor1.ch1, aggressor1.lux_raw, aggressor1.lux,
             aggressor1.ratio);
    ESP_LOGI(TAG, "  aggressor2: n=%d ch0=%u ch1=%u lux_raw=%.2f lux=%.2f ratio=%.3f",
             aggressor2_samples, aggressor2.ch0, aggressor2.ch1, aggressor2.lux_raw, aggressor2.lux,
             aggressor2.ratio);
    ESP_LOGI(TAG, "Self-cal done: k_led=%.4f k_nixie=%.4f", cal.k_led, cal.k_nixie);

    display_daemon_.restore_cal_snapshot(saved.snapshot);
    system_state_.set_auto_brightness_suppressed(saved.suppress_auto_brightness);
    reset_auto_brightness_state();
    return true;
}

void AlsDaemon::loop()
{
    ESP_LOGI(TAG, "ALS Daemon Started");

    TickType_t last_wake_time = xTaskGetTickCount();
    constexpr float kDtSec = 2.0f;

    while (true) {
        tick_ms_ += static_cast<uint32_t>(kDtSec * 1000.0f);

        if (system_state_.consume_auto_brightness_self_cal_request()) {
            run_self_calibration();
        }

        ClockSettings settings{};
        system_state_.get_settings(&settings);

        const bool should_auto = sensor_.is_ready() && settings.auto_brightness_enabled &&
                                 !system_state_.is_auto_brightness_suppressed();

        if (should_auto) {
            auto_brightness_active_ = true;

            Ltr303Sample sample{};
            if (sensor_.read_channels(&sample)) {
                AutoBrightnessCalibration cal{};
                system_state_.get_auto_brightness_calibration(&cal);

                const float lux_ir = compensate_ir(sample.lux, sample.ratio);
                const float lux_ambient =
                    subtract_self_light(lux_ir, last_backlight_, last_nixie_, cal);
                update_smoothed_lux(lux_ambient, kDtSec);
                update_calibration_learning(&cal, smoothed_lux_);
                system_state_.set_auto_brightness_calibration(cal);

                pending_factor_ = lux_to_factor(smoothed_lux_, cal);
                const uint16_t factor = apply_hysteresis(pending_factor_);
                send_ambient_factor(factor);

                last_backlight_ = display_daemon_.get_effective_backlight_brightness();
                last_nixie_ = display_daemon_.get_effective_nixie_brightness();

                AmbientLightStatus ambient{};
                ambient.lux = smoothed_lux_;
                ambient.scale = factor;
                ambient.valid = true;
                system_state_.update_ambient(ambient);

                if (++cal_save_counter_ >= kCalSaveIntervalTicks) {
                    cal_save_counter_ = 0;
                    system_state_.save_auto_brightness_calibration();
                }

                if (++log_counter_ >= kLogIntervalTicks) {
                    log_counter_ = 0;
                    ESP_LOGI(TAG, "lux=%.2f factor=%u/1024 back=%u nixie=%u", smoothed_lux_, factor,
                             last_backlight_, last_nixie_);
                }
            }
        } else if (auto_brightness_active_) {
            auto_brightness_active_ = false;
            reset_auto_brightness_state();
            send_ambient_factor(kAmbientFullScale);
        }

        vTaskDelayUntil(&last_wake_time, kPeriodTicks);
    }
}
