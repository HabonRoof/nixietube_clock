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
constexpr size_t kFrameSize = 10;

// DFPlayer commands (common subset)
constexpr uint8_t kCmdNext = 0x01;
constexpr uint8_t kCmdPrevious = 0x02;
constexpr uint8_t kCmdPlayTrack = 0x03;
constexpr uint8_t kCmdVolumeUp = 0x04;
constexpr uint8_t kCmdVolumeDown = 0x05;
constexpr uint8_t kCmdSetVolume = 0x06;
constexpr uint8_t kCmdSetEq = 0x07;
constexpr uint8_t kCmdSleep = 0x0A;
constexpr uint8_t kCmdWake = 0x0B;
constexpr uint8_t kCmdReset = 0x0C;
constexpr uint8_t kCmdPlay = 0x0D;
constexpr uint8_t kCmdPause = 0x0E;
constexpr uint8_t kCmdLoopThisTrack = 0x19;

// Query commands (datasheet: Query System Parameters)
constexpr uint8_t kCmdQueryStatus = 0x42;
constexpr uint8_t kCmdQuerySdFileCount = 0x47;
constexpr uint8_t kCmdQuerySdCurrentTrack = 0x4B;
constexpr uint8_t kRspInitOnline = 0x3F;
constexpr uint8_t kRspFeedbackBase = 0x48;

constexpr TickType_t kInterCommandDelayMs = 80;
constexpr uint32_t kPostQueryDelayMs = 150;

uint8_t clamp_volume(uint8_t volume)
{
    constexpr uint8_t kMaxVolume = 30;
    return (volume > kMaxVolume) ? kMaxVolume : volume;
}
} // namespace

DfPlayerMini::DfPlayerMini(uart_port_t uart_num)
    : uart_num_(uart_num),
      baud_rate_(9600),
      state_{.volume = 0,
             .track_number = 0,
             .track_count = 0,
             .track_count_valid = false,
             .playback_status = DfPlayerPlaybackStatus::kStopped,
             .init_info = {.received = false, .device_mask = 0},
             .looping = false,
             .low_power = false,
             .paused = false},
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
}

esp_err_t DfPlayerMini::begin(int baud_rate)
{
    baud_rate_ = baud_rate;
    drain_uart();
    initialized_ = true;
    ESP_LOGI(kLogTag, "DFPlayer ready on UART%d at %d baud", uart_num_, baud_rate_);
    return ESP_OK;
}

void DfPlayerMini::drain_uart()
{
    uint8_t scratch[64];
    int read = 0;
    do
    {
        read = uart_read_bytes(uart_num_, scratch, sizeof(scratch), 0);
    } while (read > 0);
}

bool DfPlayerMini::validate_frame_checksum(const uint8_t frame[10])
{
    if (frame[0] != kFrameStart || frame[9] != kFrameEnd)
    {
        return false;
    }

    uint16_t expected = calculate_checksum(frame[3], static_cast<uint16_t>((frame[5] << 8) | frame[6]), frame[4] != 0);
    uint16_t actual = static_cast<uint16_t>((frame[7] << 8) | frame[8]);
    return expected == actual;
}

esp_err_t DfPlayerMini::read_response_frame(ResponseFrame *out, uint32_t timeout_ms)
{
    if (!out)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[kFrameSize] = {};
    size_t filled = 0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() <= deadline)
    {
        uint8_t byte = 0;
        int read = uart_read_bytes(uart_num_, &byte, 1, pdMS_TO_TICKS(20));
        if (read <= 0)
        {
            continue;
        }

        if (filled == 0)
        {
            if (byte != kFrameStart)
            {
                continue;
            }
            buffer[0] = byte;
            filled = 1;
            continue;
        }

        buffer[filled++] = byte;
        if (filled < kFrameSize)
        {
            continue;
        }

        if (!validate_frame_checksum(buffer))
        {
            ESP_LOGW(kLogTag, "Invalid DFPlayer response checksum");
            filled = 0;
            continue;
        }

        out->command = buffer[3];
        out->parameter = static_cast<uint16_t>((buffer[5] << 8) | buffer[6]);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

void DfPlayerMini::apply_init_frame(uint16_t parameter)
{
    const uint8_t mask = static_cast<uint8_t>(parameter & 0xFF);
    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.init_info.received = true;
        state_.init_info.device_mask = mask;
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(kLogTag, "Init 0x3F received, device mask=0x%02X (SD=%s)",
             mask, (mask & kDfPlayerDeviceTfCard) ? "yes" : "no");
}

bool DfPlayerMini::is_query_response(uint8_t query_command, uint8_t response_command) const
{
    if (response_command == query_command)
    {
        return true;
    }
    // Datasheet / DFRobot: query replies often use 0x48–0x4F feedback type.
    if (response_command >= kRspFeedbackBase && response_command <= 0x4F)
    {
        return true;
    }
    return false;
}

esp_err_t DfPlayerMini::wait_for_init(uint8_t *device_mask_out, uint32_t timeout_ms)
{
    if (!initialized_)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() <= deadline)
    {
        ResponseFrame frame = {};
        const uint32_t remaining = static_cast<uint32_t>(
            (deadline - xTaskGetTickCount()) * portTICK_PERIOD_MS);
        const uint32_t slice = remaining > 50 ? 50 : (remaining > 0 ? remaining : 1);

        esp_err_t err = read_response_frame(&frame, slice);
        if (err != ESP_OK)
        {
            continue;
        }

        if (frame.command == kRspInitOnline)
        {
            apply_init_frame(frame.parameter);
            if (device_mask_out)
            {
                *device_mask_out = static_cast<uint8_t>(frame.parameter & 0xFF);
            }
            return ESP_OK;
        }

        ESP_LOGD(kLogTag, "Ignoring frame 0x%02X while waiting for init", frame.command);
    }

    ESP_LOGW(kLogTag, "Init 0x3F not received within %u ms", timeout_ms);
    return ESP_ERR_TIMEOUT;
}

DfPlayerInitInfo DfPlayerMini::init_info() const
{
    return state().init_info;
}

esp_err_t DfPlayerMini::send_query(uint8_t command, uint16_t *out_parameter, uint32_t timeout_ms)
{
    if (!out_parameter)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = send_simple_command(command, 0, false);
    if (err != ESP_OK)
    {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(kPostQueryDelayMs));

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() <= deadline)
    {
        ResponseFrame response = {};
        const uint32_t remaining = static_cast<uint32_t>(
            (deadline - xTaskGetTickCount()) * portTICK_PERIOD_MS);
        const uint32_t slice = remaining > 50 ? 50 : (remaining > 0 ? remaining : 1);

        err = read_response_frame(&response, slice);
        if (err != ESP_OK)
        {
            continue;
        }

        if (response.command == kRspInitOnline)
        {
            apply_init_frame(response.parameter);
            continue;
        }

        if (is_query_response(command, response.command))
        {
            *out_parameter = response.parameter;
            ESP_LOGI(kLogTag, "Query 0x%02X -> resp 0x%02X param %u",
                     command, response.command, response.parameter);
            return ESP_OK;
        }

        ESP_LOGW(kLogTag, "Unexpected resp 0x%02X for query 0x%02X", response.command, command);
    }

    size_t pending = 0;
    uart_get_buffered_data_len(uart_num_, &pending);
    ESP_LOGW(kLogTag, "Query 0x%02X timed out (uart_pending=%u)", command, static_cast<unsigned>(pending));
    return ESP_ERR_TIMEOUT;
}

esp_err_t DfPlayerMini::query_sd_track_count(uint16_t *out_count, uint32_t timeout_ms)
{
    if (!out_count)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t count = 0;
    esp_err_t err = send_query(kCmdQuerySdFileCount, &count, timeout_ms);
    if (err != ESP_OK)
    {
        return err;
    }

    *out_count = count;
    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.track_count = count;
        state_.track_count_valid = true;
        xSemaphoreGive(mutex_);
    }
    ESP_LOGI(kLogTag, "SD track count: %u", count);
    return ESP_OK;
}

esp_err_t DfPlayerMini::query_current_track(uint16_t *out_track, uint32_t timeout_ms)
{
    if (!out_track)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t track = 0;
    esp_err_t err = send_query(kCmdQuerySdCurrentTrack, &track, timeout_ms);
    if (err != ESP_OK)
    {
        return err;
    }

    *out_track = track;
    return ESP_OK;
}

esp_err_t DfPlayerMini::query_playback_status(DfPlayerPlaybackStatus *out_status, uint16_t *out_track,
                                              uint32_t timeout_ms)
{
    if (!out_status)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw = 0;
    esp_err_t err = send_query(kCmdQueryStatus, &raw, timeout_ms);
    if (err != ESP_OK)
    {
        return err;
    }

    uint8_t status_byte = static_cast<uint8_t>((raw >> 8) & 0xFF);
    uint16_t track = static_cast<uint16_t>(raw & 0xFF);
    if (status_byte > static_cast<uint8_t>(DfPlayerPlaybackStatus::kPaused))
    {
        status_byte = static_cast<uint8_t>(DfPlayerPlaybackStatus::kStopped);
    }

    *out_status = static_cast<DfPlayerPlaybackStatus>(status_byte);
    if (out_track)
    {
        *out_track = track;
    }

    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.playback_status = *out_status;
        state_.paused = (*out_status == DfPlayerPlaybackStatus::kPaused);
        if (track > 0)
        {
            state_.track_number = track;
        }
        xSemaphoreGive(mutex_);
    }

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
        state_.playback_status = DfPlayerPlaybackStatus::kPlaying;
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
        state_.track_number = 0;
        state_.paused = false;
        state_.playback_status = DfPlayerPlaybackStatus::kPlaying;
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
        state_.playback_status = DfPlayerPlaybackStatus::kPlaying;
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
        state_.playback_status = DfPlayerPlaybackStatus::kPaused;
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
        state_.playback_status = DfPlayerPlaybackStatus::kPlaying;
        state_.low_power = false;
        xSemaphoreGive(mutex_);
    }
    return err;
}

esp_err_t DfPlayerMini::stop()
{
    esp_err_t err = send_simple_command(kCmdLoopThisTrack, 0);
    if (err == ESP_OK)
    {
        err = pause();
    }
    if (err == ESP_OK && mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
        state_.playback_status = DfPlayerPlaybackStatus::kStopped;
        xSemaphoreGive(mutex_);
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
        state_ = AudioPlaybackState{.volume = 0,
                                    .track_number = 0,
                                    .track_count = 0,
                                    .track_count_valid = false,
                                    .playback_status = DfPlayerPlaybackStatus::kStopped,
                                    .init_info = {.received = false, .device_mask = 0},
                                    .looping = false,
                                    .low_power = false,
                                    .paused = false};
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
    (void)baud_rate;
    return ESP_OK;
}

esp_err_t DfPlayerMini::send_simple_command(uint8_t command, uint16_t parameter, bool request_feedback)
{
    if (!initialized_)
    {
        ESP_LOGE(kLogTag, "DFPlayer not initialized; call begin() first");
        return ESP_ERR_INVALID_STATE;
    }

    if (mutex_)
    {
        xSemaphoreTake(mutex_, portMAX_DELAY);
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
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
        return ESP_FAIL;
    }

    esp_err_t err = uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(kDefaultTxWaitMs));
    if (err != ESP_OK)
    {
        ESP_LOGW(kLogTag, "UART TX did not complete: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(kInterCommandDelayMs));

    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
    return err;
}

uint16_t DfPlayerMini::calculate_checksum(uint8_t command, uint16_t parameter, bool request_feedback)
{
    uint16_t sum = kFrameVersion + kFrameLength + command + (request_feedback ? 0x01 : 0x00);
    sum += static_cast<uint8_t>((parameter >> 8) & 0xFF);
    sum += static_cast<uint8_t>(parameter & 0xFF);

    return static_cast<uint16_t>(0xFFFF - sum + 1);
}
