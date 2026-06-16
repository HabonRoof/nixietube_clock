#include "daemons/audio_daemon.h"
#include "power_controller.h"
#include "esp_log.h"

static const char *TAG = "AudioDaemon";

AudioDaemon::AudioDaemon(IAudioDriver &driver, PowerController &power_controller)
    : driver_(driver),
      power_controller_(power_controller),
      queue_(nullptr),
      task_handle_(nullptr),
      dfplayer_powered_(false)
{
    queue_ = xQueueCreate(10, sizeof(AudioMessage));
}

AudioDaemon::~AudioDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
    if (queue_) {
        vQueueDelete(queue_);
    }
}

void AudioDaemon::start()
{
    xTaskCreate(task_entry, "audio_daemon", 4096, this, 4, &task_handle_);
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
        vTaskDelay(pdMS_TO_TICKS(450));
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
            ensure_dfplayer_power();
            driver_.play_track(msg.param.track_number);
            break;
        case AudioCmd::STOP:
            ensure_dfplayer_power();
            driver_.stop();
            break;
        case AudioCmd::PAUSE:
            ensure_dfplayer_power();
            driver_.pause();
            break;
        case AudioCmd::RESUME:
            ensure_dfplayer_power();
            driver_.resume();
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
            ensure_dfplayer_power();
            driver_.play_next();
            break;
        case AudioCmd::PREVIOUS:
            ensure_dfplayer_power();
            driver_.play_previous();
            break;
        default:
            break;
    }
}
