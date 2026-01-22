#include "led_driver.h"
#include "ws2812.h"

LedDriver::LedDriver(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t copy_encoder)
    : strip_(new Ws2812Strip(tx_channel, copy_encoder))
{
}

LedDriver::~LedDriver()
{
    delete strip_;
}

std::size_t LedDriver::get_led_count() const
{
    return strip_->get_led_count();
}

esp_err_t LedDriver::set_pixel(std::size_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    return strip_->set_pixel(index, red, green, blue);
}

esp_err_t LedDriver::show()
{
    return strip_->show();
}

esp_err_t LedDriver::fill(uint8_t red, uint8_t green, uint8_t blue)
{
    return strip_->fill(red, green, blue);
}

esp_err_t LedDriver::clear()
{
    return strip_->fill(0, 0, 0);
}