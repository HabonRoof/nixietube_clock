#include "audio_setup.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <map>
#include <string>

namespace
{
constexpr const char *kLogTag = "audio_setup";
constexpr uint32_t kPlayerInitDelayMs = 250; // allow DFPlayer to boot after UART init
} // namespace

AudioConfig make_default_audio_config()
{
    AudioConfig config{};
    config.uart_port = UART_NUM_1;
    config.tx_pin = GPIO_NUM_18; // MCU TX to DFPlayer RX
    config.rx_pin = GPIO_NUM_17; // MCU RX from DFPlayer TX
    config.baud_rate = 9600;
    config.default_volume = 20;
    config.default_track = 2;
    config.loop_on_start = false;
    config.start_in_low_power = false;
    return config;
}

void initialize_audio_module(const AudioConfig &config, AudioControlHandles &handles)
{
    static DfPlayerMini player(config.uart_port, config.tx_pin, config.rx_pin);
    static const std::map<uint16_t, std::string> kTrackNames = {
        {1, "烏龍派出所"},
        {2, "Amadues"},
        {3, "steins gate ending2"},
    };

    esp_err_t err = player.begin(config.baud_rate);
    if (err != ESP_OK)
    {
        ESP_LOGE(kLogTag, "Failed to start DFPlayer module");
        handles = AudioControlHandles{.player = nullptr};
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
    player.set_track_names(kTrackNames);
    player.set_volume(config.default_volume);

    if (config.default_track > 0)
    {
        player.play_track(config.default_track);
        vTaskDelay(pdMS_TO_TICKS(kPlayerInitDelayMs));
    }

    player.set_loop(config.loop_on_start);

    if (config.start_in_low_power)
    {
        player.set_low_power_mode(true);
        vTaskDelay(pdMS_TO_TICKS(kPlayerInitDelayMs));
    }

    handles = AudioControlHandles{
        .player = &player,
    };
}
