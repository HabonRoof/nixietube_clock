#pragma once

#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstdint>
#include <map>
#include <string>

// DFR0299 DFPlayer Mini — EQ presets (serial command 0x07)
enum class DfPlayerEqPreset : uint8_t
{
    kNormal = 0,
    kPop = 1,
    kRock = 2,
    kJazz = 3,
    kClassic = 4,
    kBass = 5,
};

enum class DfPlayerPlaybackStatus : uint8_t
{
    kStopped = 0,
    kPlaying = 1,
    kPaused = 2,
};

// TODO: Add storage type for query_status command
enum class DfPlayerStorageType : uint8_t
{
    kUnknown = 0,
    kUsbDisk = 1,
    kSdCard = 2,
};

// SD:/mp3/NNNN.mp3 file numbers (command 0x12).
inline constexpr uint16_t kDfPlayerMp3MinFile = 1;
inline constexpr uint16_t kDfPlayerMp3MaxFile = 2999;

struct AudioPlaybackState
{
    uint8_t volume;
    uint16_t track_number;
    uint16_t track_count;
    bool track_count_valid;
    DfPlayerPlaybackStatus playback_status;
    bool looping;
    bool low_power;
    bool paused;
};

class DfPlayerMini
{
public:
    explicit DfPlayerMini(uart_port_t uart_num);
    ~DfPlayerMini();
    DfPlayerMini(const DfPlayerMini &) = delete;
    DfPlayerMini &operator=(const DfPlayerMini &) = delete;

    esp_err_t begin(int baud_rate = 9600);
    // Play SD:/mp3/NNNN.mp3 via command 0x12 (file_number 1..2999).
    esp_err_t play_from_mp3_folder(uint16_t file_number);
    esp_err_t play_next();
    esp_err_t play_previous();
    esp_err_t pause();
    esp_err_t resume();
    esp_err_t stop();
    esp_err_t set_volume(uint8_t volume);
    esp_err_t volume_up();
    esp_err_t volume_down();
    esp_err_t set_loop(bool enable);
    esp_err_t set_low_power_mode(bool enable);
    esp_err_t set_eq(DfPlayerEqPreset eq);
    esp_err_t reset();

    esp_err_t query_sd_track_count(uint16_t *out_count, uint32_t timeout_ms = 800);
    esp_err_t query_current_track(uint16_t *out_track, uint32_t timeout_ms = 500);
    esp_err_t query_playback_status(DfPlayerPlaybackStatus *out_status, uint16_t *out_track,
                                    uint32_t timeout_ms = 500);

    AudioPlaybackState state() const;
    void set_track_names(const std::map<uint16_t, std::string> &track_names);

private:
    struct ResponseFrame
    {
        uint8_t command;
        uint16_t parameter;
    };

    esp_err_t send_command(uint8_t command, uint16_t parameter = 0, bool request_feedback = false);
    esp_err_t send_query(uint8_t command, uint16_t *out_parameter, uint32_t timeout_ms);
    esp_err_t read_response_frame(ResponseFrame *out, uint32_t timeout_ms);
    bool is_query_response(uint8_t query_command, uint8_t response_command) const;
    void drain_uart();
    static uint16_t calculate_checksum(uint8_t command, uint16_t parameter, bool request_feedback);
    static bool validate_frame_checksum(const uint8_t frame[10]);

    uart_port_t uart_num_;
    int baud_rate_;
    AudioPlaybackState state_;
    std::map<uint16_t, std::string> track_names_;
    mutable SemaphoreHandle_t mutex_;
    bool initialized_;
};
