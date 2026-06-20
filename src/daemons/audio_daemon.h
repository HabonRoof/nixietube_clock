#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "message_types.h"
#include "audio_driver.h"

class PowerController;

class AudioDaemon
{
public:
    AudioDaemon(IAudioDriver &driver, PowerController &power_controller, uint8_t boot_volume = 20);
    ~AudioDaemon();

    void start();
    QueueHandle_t get_queue() const;

    bool rpc_query_tracks(uint16_t *count_out, uint32_t timeout_ms = 2500);
    bool rpc_get_status(AudioDaemonStatus *out, uint32_t timeout_ms = 500);
    bool rpc_toggle_track(uint16_t track, AudioDaemonStatus *out, uint32_t timeout_ms = 500);
    void snapshot_status(AudioDaemonStatus *out) const;

private:
    static void task_entry(void *param);
    static void boot_play_timer_cb(void *arg);
    static void boot_stop_timer_cb(void *arg);

    void loop();
    void process_message(const AudioMessage &msg);
    void ensure_dfplayer_power();
    void schedule_boot_chime();
    void cancel_boot_chime();
    void stop_boot_chime_timers();
    void fill_status(AudioDaemonStatus *out) const;
    void toggle_track(uint16_t track);
    bool send_rpc(AudioMessage *msg, uint32_t timeout_ms);
    void complete_rpc(const AudioMessage &msg, bool ok);

    static constexpr uint16_t kBootChimeTrack = 3;
    static constexpr uint32_t kBootPlayDelayMs = 3000;
    static constexpr uint32_t kBootPlayDurationMs = 10000;
    static constexpr uint16_t kKnownSdTrackCount = 10;

    IAudioDriver &driver_;
    PowerController &power_controller_;
    QueueHandle_t queue_;
    TaskHandle_t task_handle_;
    esp_timer_handle_t boot_play_timer_;
    esp_timer_handle_t boot_stop_timer_;
    bool dfplayer_powered_;
    bool boot_chime_active_;
    uint16_t track_count_;
    bool track_count_valid_;
    uint16_t current_track_;
    AudioPlaybackUiState playback_state_;
    uint8_t boot_volume_;
};
