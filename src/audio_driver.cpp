#include "audio_driver.h"

AudioDriver::AudioDriver(uart_port_t uart_num)
    : player_(uart_num)
{
    player_.begin();
}

esp_err_t AudioDriver::play_track(uint16_t track_number)
{
    return player_.play_track(track_number);
}

esp_err_t AudioDriver::stop()
{
    return player_.stop();
}

esp_err_t AudioDriver::pause()
{
    return player_.pause();
}

esp_err_t AudioDriver::resume()
{
    return player_.resume();
}

esp_err_t AudioDriver::set_volume(uint8_t volume)
{
    return player_.set_volume(volume);
}

esp_err_t AudioDriver::volume_up()
{
    return player_.volume_up();
}

esp_err_t AudioDriver::volume_down()
{
    return player_.volume_down();
}

esp_err_t AudioDriver::play_next()
{
    return player_.play_next();
}

esp_err_t AudioDriver::play_previous()
{
    return player_.play_previous();
}