#include "audio_driver.h"

AudioDriver::AudioDriver(uart_port_t uart_num)
    : player_(uart_num),
      initialized_(false)
{
}

void AudioDriver::ensure_initialized()
{
    if (!initialized_) {
        player_.begin();
        initialized_ = true;
    }
}

esp_err_t AudioDriver::play_track(uint16_t track_number)
{
    ensure_initialized();
    return player_.play_track(track_number);
}

esp_err_t AudioDriver::stop()
{
    ensure_initialized();
    return player_.stop();
}

esp_err_t AudioDriver::pause()
{
    ensure_initialized();
    return player_.pause();
}

esp_err_t AudioDriver::resume()
{
    ensure_initialized();
    return player_.resume();
}

esp_err_t AudioDriver::set_volume(uint8_t volume)
{
    ensure_initialized();
    return player_.set_volume(volume);
}

esp_err_t AudioDriver::volume_up()
{
    ensure_initialized();
    return player_.volume_up();
}

esp_err_t AudioDriver::volume_down()
{
    ensure_initialized();
    return player_.volume_down();
}

esp_err_t AudioDriver::play_next()
{
    ensure_initialized();
    return player_.play_next();
}

esp_err_t AudioDriver::play_previous()
{
    ensure_initialized();
    return player_.play_previous();
}
