#include "dfplayer_mini.h"

#include "esp_log.h"
#include "freertos/task.h"

namespace
{
constexpr const char *kLogTag = "dfplayer";
constexpr uint8_t kFrameStart = 0x7E;
constexpr uint8_t kFrameVersion = 0xFF;
constexpr uint8_t kFrameLength = 0x06;
constexpr uint8_t kFrameEnd = 0xEF;
constexpr int kDefaultTxWaitMs = 20;

// DFPlayer commands (common subset)
constexpr uint8_t kCmdNext = 0x01;
constexpr uint8_t kCmdPrevious = 0x02;
constexpr uint8_t kCmdPlayTrack = 0x03;
constexpr uint8_t kCmdVolumeUp = 0x04;
constexpr uint8_t kCmdVolumeDown = 0x05;
constexpr uint8_t kCmdSetVolume = 0x06;
constexpr uint8_t kCmdSetEq = 0x07;
constexpr uint8_t kCmdSetPlaybackMode = 0x08; // loop: 0=loop all, 1=loop folder, 2=single repeat, 3=random
constexpr uint8_t kCmdSleep = 0x0A;
constexpr uint8_t kCmdWake = 0x0B;
constexpr uint8_t kCmdReset = 0x0C;
constexpr uint8_t kCmdPlay = 0x0D;
constexpr uint8_t kCmdPause = 0x0E;
constexpr uint8_t kCmdLoopThisTrack = 0x19;
constexpr TickType_t kInterCommandDelayMs = 80; // give DFPlayer breathing room between commands

uint8_t clamp_volume(uint8_t volume)
{
    constexpr uint8_t kMaxVolume = 30;
    return (volume > kMaxVolume) ? kMaxVolume : volume;
}
} // namespace

DfPlayerMini::DfPlayerMini(uart_port_t uart_num)
    : uart_num_(uart_num),
      baud_rate_(9600),
      state_{.volume = 0, .track_number = 0, .looping = false, .low_power = false, .paused = false},
      mutex_(xSemaphoreCreateMutex()),
      initialized_(false)
{
}

DfPlayerMini::~DfPlayerMini()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }

    if (initialized_)
    {
        // We don't delete driver as it might be shared or managed externally
    }
}

esp_err_t DfPlayerMini::begin(int baud_rate)
{
    baud_rate_ = baud_rate;
    // UART is assumed to be initialized externally
    initialized_ = true;
    ESP_LOGI(kLogTag, "DFPlayer ready on UART%d at %d baud", uart_num_, baud_rate_);
    return ESP_OK;
}

esp_err_t DfPlayerMini::play_track(uint16_t track_number)
{
    esp_err_t err = send_simple_command(kCmdPlayTrack, track_number);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.track_number = track_number;
        state_.paused = false;
        state_.low_power = false;
        auto it = track_names_.find(track_number);
        if (it != track_names_.end())
        {
            ESP_LOGI(kLogTag, "Playing track %u: %s", track_number, it->second.c_str());
        }
        else
        {
            ESP_LOGI(kLogTag, "Playing track %u", track_number);
        }
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::play_next()
{
    esp_err_t err = send_simple_command(kCmdNext);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.track_number = 0; // unknown until queried; we just stepped
        state_.paused = false;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::play_previous()
{
    esp_err_t err = send_simple_command(kCmdPrevious);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.track_number = 0;
        state_.paused = false;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::pause()
{
    esp_err_t err = send_simple_command(kCmdPause);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.paused = true;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::resume()
{
    esp_err_t err = send_simple_command(kCmdPlay);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.paused = false;
        state_.low_power = false;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::stop()
{
    // DFPlayer lacks a dedicated stop; loop mode off and pause acts as a soft stop.
    esp_err_t err = send_simple_command(kCmdLoopThisTrack, 0);
    if (err == ESP_OK)
    {
        err = pause();
    }
    return err;
}

esp_err_t DfPlayerMini::set_volume(uint8_t volume)
{
    uint8_t clamped = clamp_volume(volume);
    esp_err_t err = send_simple_command(kCmdSetVolume, clamped);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.volume = clamped;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::volume_up()
{
    esp_err_t err = send_simple_command(kCmdVolumeUp);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.volume = clamp_volume(state_.volume + 1);
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::volume_down()
{
    esp_err_t err = send_simple_command(kCmdVolumeDown);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.volume = (state_.volume == 0) ? 0 : static_cast<uint8_t>(state_.volume - 1);
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::set_loop(bool enable)
{
    // 0 stops single-track loop, 1 starts it.
    esp_err_t err = send_simple_command(kCmdLoopThisTrack, enable ? 1 : 0);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.looping = enable;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::set_low_power_mode(bool enable)
{
    uint8_t command = enable ? kCmdSleep : kCmdWake;
    esp_err_t err = send_simple_command(command);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.low_power = enable;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::set_eq(DfPlayerEqPreset eq)
{
    uint8_t preset = static_cast<uint8_t>(eq);
    if (preset > static_cast<uint8_t>(DfPlayerEqPreset::kBass))
    {
        preset = static_cast<uint8_t>(DfPlayerEqPreset::kNormal);
    }
    return send_simple_command(kCmdSetEq, preset);
}

esp_err_t DfPlayerMini::reset()
{
    esp_err_t err = send_simple_command(kCmdReset);
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_ = AudioPlaybackState{.volume = 0, .track_number = 0, .looping = false, .low_power = false, .paused = false};
        xSemaphoreGive(mutex_);
    }
    return err;
}

AudioPlaybackState DfPlayerMini::state() const
{
    AudioPlaybackState snapshot = state_;
    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        snapshot = state_;
        xSemaphoreGive(mutex_);
    }
    return snapshot;
}

void DfPlayerMini::set_track_names(const std::map<uint16_t, std::string> &track_names)
{
    if (!mutex_)
    {
        track_names_ = track_names;
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    track_names_ = track_names;
    xSemaphoreGive(mutex_);
}

esp_err_t DfPlayerMini::configure_uart(int baud_rate)
{
    // Deprecated/Unused in centralized init mode
    return ESP_OK;
}

esp_err_t DfPlayerMini::send_simple_command(uint8_t command, uint16_t parameter, bool request_feedback)
{
    if (!initialized_)
    {
        ESP_LOGE(kLogTag, "DFPlayer not initialized; call begin() first");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t frame[10];
    frame[0] = kFrameStart;
    frame[1] = kFrameVersion;
    frame[2] = kFrameLength;
    frame[3] = command;
    frame[4] = request_feedback ? 0x01 : 0x00;
    frame[5] = static_cast<uint8_t>((parameter >> 8) & 0xFF);
    frame[6] = static_cast<uint8_t>(parameter & 0xFF);

    uint16_t checksum = calculate_checksum(command, parameter, request_feedback);
    frame[7] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
    frame[8] = static_cast<uint8_t>(checksum & 0xFF);
    frame[9] = kFrameEnd;

    int written = uart_write_bytes(uart_num_, reinterpret_cast<const char *>(frame), sizeof(frame));
    if (written < 0)
    {
        ESP_LOGE(kLogTag, "UART write failed for command 0x%02X", command);
        return ESP_FAIL;
    }

    esp_err_t err = uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(kDefaultTxWaitMs));
    if (err != ESP_OK)
    {
        ESP_LOGW(kLogTag, "UART TX did not complete: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(kInterCommandDelayMs));
    return err;
}

uint16_t DfPlayerMini::calculate_checksum(uint8_t command, uint16_t parameter, bool request_feedback)
{
    uint16_t sum = kFrameVersion + kFrameLength + command + (request_feedback ? 0x01 : 0x00);
    sum += static_cast<uint8_t>((parameter >> 8) & 0xFF);
    sum += static_cast<uint8_t>(parameter & 0xFF);

    // Two's complement over the 16-bit checksum field.
    return static_cast<uint16_t>(0xFFFF - sum + 1);
}
