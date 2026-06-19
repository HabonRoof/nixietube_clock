#include "daemons/audio_daemon.h"
#include "power_controller.h"
#include "esp_log.h"
#include <new>

static const char *TAG = "AudioDaemon";

AudioDaemon::AudioDaemon(IAudioDriver &driver, PowerController &power_controller, uint8_t boot_volume)
    : driver_(driver),
      power_controller_(power_controller),
      queue_(nullptr),
      task_handle_(nullptr),
      boot_play_timer_(nullptr),
      boot_stop_timer_(nullptr),
      dfplayer_powered_(false),
      boot_chime_active_(false),
      dfplayer_init_ok_(false),
      sd_card_online_(false),
      device_mask_(0),
      track_count_(0),
      track_count_valid_(false),
      current_track_(0),
      playback_state_(AudioPlaybackUiState::STOPPED),
      boot_volume_(boot_volume)
{
    queue_ = xQueueCreate(16, sizeof(AudioMessage));
}

AudioDaemon::~AudioDaemon()
{
    stop_boot_chime_timers();
    if (boot_play_timer_) {
        esp_timer_delete(boot_play_timer_);
        boot_play_timer_ = nullptr;
    }
    if (boot_stop_timer_) {
        esp_timer_delete(boot_stop_timer_);
        boot_stop_timer_ = nullptr;
    }
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (queue_) {
        vQueueDelete(queue_);
    }
}

void AudioDaemon::start()
{
    const esp_timer_create_args_t play_args = {
        .callback = &AudioDaemon::boot_play_timer_cb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "boot_play",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&play_args, &boot_play_timer_));

    const esp_timer_create_args_t stop_args = {
        .callback = &AudioDaemon::boot_stop_timer_cb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "boot_stop",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&stop_args, &boot_stop_timer_));

    xTaskCreate(task_entry, "audio_daemon", 4096, this, 4, &task_handle_);
    schedule_boot_chime();
}

void AudioDaemon::schedule_boot_chime()
{
    if (boot_play_timer_) {
        esp_timer_start_once(boot_play_timer_, kBootPlayDelayMs * 1000ULL);
        ESP_LOGI(TAG, "Boot chime scheduled in %u ms (track %u, %u ms)",
                 kBootPlayDelayMs, kBootChimeTrack, kBootPlayDurationMs);
    }
}

void AudioDaemon::stop_boot_chime_timers()
{
    if (boot_play_timer_) {
        esp_timer_stop(boot_play_timer_);
    }
    if (boot_stop_timer_) {
        esp_timer_stop(boot_stop_timer_);
    }
    boot_chime_active_ = false;
}

void AudioDaemon::cancel_boot_chime()
{
    stop_boot_chime_timers();
}

void AudioDaemon::boot_play_timer_cb(void *arg)
{
    auto *daemon = static_cast<AudioDaemon *>(arg);
    AudioMessage msg = {};
    msg.command = AudioCmd::BOOT_CHIME_PLAY;
    xQueueSend(daemon->queue_, &msg, 0);
}

void AudioDaemon::boot_stop_timer_cb(void *arg)
{
    auto *daemon = static_cast<AudioDaemon *>(arg);
    AudioMessage msg = {};
    msg.command = AudioCmd::BOOT_CHIME_STOP;
    xQueueSend(daemon->queue_, &msg, 0);
}

QueueHandle_t AudioDaemon::get_queue() const
{
    return queue_;
}

void AudioDaemon::ensure_dfplayer_power()
{
    if (!dfplayer_powered_) {
        power_controller_.set_dfplayer_enabled(true);
        dfplayer_powered_ = true;
        wait_dfplayer_init();
    }
}

bool AudioDaemon::wait_dfplayer_init()
{
    uint8_t mask = 0;
    esp_err_t err = driver_.wait_for_init(&mask, 2000);
    dfplayer_init_ok_ = (err == ESP_OK);
    device_mask_ = mask;
    sd_card_online_ = dfplayer_init_ok_ && ((mask & kDfPlayerDeviceTfCard) != 0);

    if (dfplayer_init_ok_) {
        ESP_LOGI(TAG, "DFPlayer init OK, mask=0x%02X, SD=%s",
                 mask, sd_card_online_ ? "online" : "missing");
    } else {
        ESP_LOGW(TAG, "DFPlayer init 0x3F not received within 2s");
    }
    return dfplayer_init_ok_;
}

void AudioDaemon::fill_status(AudioDaemonStatus *out) const
{
    if (!out) {
        return;
    }
    out->track_count = track_count_;
    out->current_track = current_track_;
    out->state = playback_state_;
    out->track_count_valid = track_count_valid_;
    out->dfplayer_init_ok = dfplayer_init_ok_;
    out->sd_card_online = sd_card_online_;
    out->device_mask = device_mask_;
}

bool AudioDaemon::send_rpc(AudioMessage *msg, uint32_t timeout_ms)
{
    if (!msg || !queue_) {
        return false;
    }

    auto *ctx = new (std::nothrow) AudioRpcContext{};
    if (!ctx) {
        return false;
    }

    ctx->sem = xSemaphoreCreateBinary();
    if (!ctx->sem) {
        delete ctx;
        return false;
    }

    ctx->ok = false;
    ctx->abandoned = false;
    msg->rpc_ctx = ctx;

    if (xQueueSend(queue_, msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        vSemaphoreDelete(ctx->sem);
        delete ctx;
        msg->rpc_ctx = nullptr;
        return false;
    }

    if (xSemaphoreTake(ctx->sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ctx->abandoned = true;
        return false;
    }

    const bool ok = ctx->ok;
    vSemaphoreDelete(ctx->sem);
    delete ctx;
    msg->rpc_ctx = nullptr;
    return ok;
}

void AudioDaemon::complete_rpc(const AudioMessage &msg, bool ok)
{
    AudioRpcContext *ctx = msg.rpc_ctx;
    if (!ctx) {
        return;
    }

    if (ctx->abandoned) {
        vSemaphoreDelete(ctx->sem);
        delete ctx;
        return;
    }

    ctx->ok = ok;
    xSemaphoreGive(ctx->sem);
}

bool AudioDaemon::rpc_query_tracks(uint16_t *count_out, uint32_t timeout_ms)
{
    if (!count_out) {
        return false;
    }

    AudioMessage msg = {};
    msg.command = AudioCmd::QUERY_TRACK_COUNT;
    msg.response_count = count_out;

    if (!send_rpc(&msg, timeout_ms)) {
        return false;
    }

    return track_count_valid_;
}

bool AudioDaemon::rpc_get_status(AudioDaemonStatus *out, uint32_t timeout_ms)
{
    if (!out) {
        return false;
    }

    AudioMessage msg = {};
    msg.command = AudioCmd::GET_STATUS;
    msg.response_status = out;

    return send_rpc(&msg, timeout_ms);
}

bool AudioDaemon::rpc_toggle_track(uint16_t track, AudioDaemonStatus *out, uint32_t timeout_ms)
{
    AudioMessage msg = {};
    msg.command = AudioCmd::TOGGLE_TRACK;
    msg.param.track_number = track;
    msg.response_status = out;

    return send_rpc(&msg, timeout_ms);
}

void AudioDaemon::snapshot_status(AudioDaemonStatus *out) const
{
    fill_status(out);
}

void AudioDaemon::toggle_track(uint16_t track)
{
    cancel_boot_chime();

    if (track == 0) {
        return;
    }

    ensure_dfplayer_power();

    if (current_track_ == track && playback_state_ == AudioPlaybackUiState::PLAYING) {
        if (driver_.pause() == ESP_OK) {
            playback_state_ = AudioPlaybackUiState::PAUSED;
        }
        return;
    }

    if (current_track_ == track && playback_state_ == AudioPlaybackUiState::PAUSED) {
        if (driver_.resume() == ESP_OK) {
            playback_state_ = AudioPlaybackUiState::PLAYING;
        }
        return;
    }

    if (driver_.play_track(track) == ESP_OK) {
        current_track_ = track;
        playback_state_ = AudioPlaybackUiState::PLAYING;
    }
}

void AudioDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<AudioDaemon *>(param);
    daemon->loop();
}

void AudioDaemon::loop()
{
    ESP_LOGI(TAG, "Audio Daemon Started");

    while (true) {
        AudioMessage msg;
        if (xQueueReceive(queue_, &msg, portMAX_DELAY) == pdTRUE) {
            process_message(msg);
        }
    }
}

void AudioDaemon::process_message(const AudioMessage &msg)
{
    switch (msg.command) {
        case AudioCmd::PLAY_TRACK:
            cancel_boot_chime();
            ensure_dfplayer_power();
            if (driver_.play_track(msg.param.track_number) == ESP_OK) {
                current_track_ = msg.param.track_number;
                playback_state_ = AudioPlaybackUiState::PLAYING;
            }
            break;
        case AudioCmd::STOP:
            ensure_dfplayer_power();
            if (driver_.stop() == ESP_OK) {
                playback_state_ = AudioPlaybackUiState::STOPPED;
            }
            break;
        case AudioCmd::PAUSE:
            ensure_dfplayer_power();
            if (driver_.pause() == ESP_OK) {
                playback_state_ = AudioPlaybackUiState::PAUSED;
            }
            break;
        case AudioCmd::RESUME:
            ensure_dfplayer_power();
            if (driver_.resume() == ESP_OK) {
                playback_state_ = AudioPlaybackUiState::PLAYING;
            }
            break;
        case AudioCmd::SET_VOLUME:
            ensure_dfplayer_power();
            driver_.set_volume(msg.param.volume);
            break;
        case AudioCmd::VOLUME_UP:
            ensure_dfplayer_power();
            driver_.volume_up();
            break;
        case AudioCmd::VOLUME_DOWN:
            ensure_dfplayer_power();
            driver_.volume_down();
            break;
        case AudioCmd::NEXT:
            cancel_boot_chime();
            ensure_dfplayer_power();
            driver_.play_next();
            playback_state_ = AudioPlaybackUiState::PLAYING;
            break;
        case AudioCmd::PREVIOUS:
            cancel_boot_chime();
            ensure_dfplayer_power();
            driver_.play_previous();
            playback_state_ = AudioPlaybackUiState::PLAYING;
            break;
        case AudioCmd::QUERY_TRACK_COUNT: {
            ensure_dfplayer_power();
            if (!dfplayer_init_ok_) {
                complete_rpc(msg, false);
                break;
            }
            if (!sd_card_online_) {
                ESP_LOGW(TAG, "SD card not reported in init mask 0x%02X", device_mask_);
                complete_rpc(msg, false);
                break;
            }
            uint16_t count = 0;
            bool ok = (driver_.query_sd_track_count(&count, 1000) == ESP_OK);
            if (ok) {
                track_count_ = count;
                track_count_valid_ = true;
                if (msg.response_count) {
                    *msg.response_count = count;
                }
            }
            complete_rpc(msg, ok);
            break;
        }
        case AudioCmd::GET_STATUS: {
            // Use cached daemon state; avoid UART query during playback (slow/unreliable).
            if (msg.response_status) {
                fill_status(msg.response_status);
            }
            complete_rpc(msg, true);
            break;
        }
        case AudioCmd::TOGGLE_TRACK:
            toggle_track(msg.param.track_number);
            if (msg.response_status) {
                fill_status(msg.response_status);
            }
            complete_rpc(msg, true);
            break;
        case AudioCmd::BOOT_CHIME_PLAY:
            ensure_dfplayer_power();
            driver_.set_volume(boot_volume_);
            if (driver_.play_track(kBootChimeTrack) == ESP_OK) {
                current_track_ = kBootChimeTrack;
                playback_state_ = AudioPlaybackUiState::PLAYING;
                boot_chime_active_ = true;
                if (boot_stop_timer_) {
                    esp_timer_start_once(boot_stop_timer_, kBootPlayDurationMs * 1000ULL);
                }
                ESP_LOGI(TAG, "Boot chime playing track %u", kBootChimeTrack);
            }
            break;
        case AudioCmd::BOOT_CHIME_STOP:
            if (boot_chime_active_) {
                driver_.stop();
                playback_state_ = AudioPlaybackUiState::STOPPED;
                boot_chime_active_ = false;
                ESP_LOGI(TAG, "Boot chime stopped");
            }
            break;
        default:
            complete_rpc(msg, false);
            break;
    }
}
