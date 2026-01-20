#include "daemons/audio_daemon.h"
#include "esp_log.h"

static const char *TAG = "AudioDaemon";

AudioDaemon::AudioDaemon(IAudioDriver &driver)
    : driver_(driver),
      queue_(nullptr),
      task_handle_(nullptr)
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
            driver_.play_track(msg.param.track_number);
            break;
        case AudioCmd::STOP:
            driver_.stop();
            break;
        case AudioCmd::PAUSE:
            driver_.pause();
            break;
        case AudioCmd::RESUME:
            driver_.resume();
            break;
        case AudioCmd::SET_VOLUME:
            driver_.set_volume(msg.param.volume);
            break;
        case AudioCmd::VOLUME_UP:
            driver_.volume_up();
            break;
        case AudioCmd::VOLUME_DOWN:
            driver_.volume_down();
            break;
        case AudioCmd::NEXT:
            driver_.play_next();
            break;
        case AudioCmd::PREVIOUS:
            driver_.play_previous();
            break;
        default:
            break;
    }
}