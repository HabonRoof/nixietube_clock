#pragma once

#include <cstdint>
#include <array>
#include "pca9685/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Abstract interface for nixie display hardware.
class INixieDriver
{
public:
    virtual ~INixieDriver() = default;
    virtual void display_time(uint8_t h, uint8_t m, uint8_t s) = 0;
    virtual void display_date(uint8_t yy, uint8_t mm, uint8_t dd) = 0;
    virtual void display_number(uint32_t number) = 0;
    virtual void set_brightness(uint8_t brightness) = 0;
    virtual void set_digits(const std::array<uint8_t, 6> &digits) = 0;
    virtual void set_digit_at(size_t tube_index, uint8_t digit) = 0;
    virtual void set_tube_brightness(size_t tube_index, uint8_t scale) = 0;
    virtual void nixie_scan_start(i2c_port_t i2c_port) = 0;
};

class NixieDriver : public INixieDriver
{
public:
    static constexpr size_t kTubeCount = 6;

    NixieDriver();
    ~NixieDriver() override = default;

    void display_time(uint8_t h, uint8_t m, uint8_t s) override;
    void display_date(uint8_t yy, uint8_t mm, uint8_t dd) override;
    void display_number(uint32_t number) override;
    void set_brightness(uint8_t brightness) override;
    void set_digits(const std::array<uint8_t, 6> &digits) override;
    void set_digit_at(size_t tube_index, uint8_t digit) override;
    void set_tube_brightness(size_t tube_index, uint8_t scale) override;
    void nixie_scan_start(i2c_port_t i2c_port) override;

private:
    struct ChannelRef
    {
        uint8_t chip_index;
        uint8_t channel;

        bool operator==(const ChannelRef &other) const
        {
            return chip_index == other.chip_index && channel == other.channel;
        }
    };

    static void scan_task_entry(void *param);
    void scan_loop();
    void push_all_cathodes(std::array<Pca9685, 4> &pca);
    void flush_chip(Pca9685 &chip, uint8_t chip_index);

    std::array<uint8_t, kTubeCount> digit_cache_{};
    std::array<uint8_t, kTubeCount> tube_brightness_{};
    std::array<ChannelRef, kTubeCount> active_cathode_{};
    std::array<std::array<uint16_t, 16>, 4> cathode_shadow_{};
    std::array<std::array<uint16_t, 16>, 4> flushed_shadow_{};
    std::array<bool, 4> chip_dirty_{};
    uint8_t brightness_ = 0;
    volatile bool pwm_dirty_ = true;
    TaskHandle_t scan_task_ = nullptr;
    i2c_port_t i2c_port_ = I2C_NUM_0;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
