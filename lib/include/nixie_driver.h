#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include "nixie_tube.h"
#include "pca9685/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Abstract Interface for Nixie Driver
class INixieDriver
{
public:
    virtual ~INixieDriver() = default;
    virtual void display_time(uint8_t h, uint8_t m, uint8_t s) = 0;
    virtual void display_date(uint8_t yy, uint8_t mm, uint8_t dd) = 0;
    virtual void display_number(uint32_t number) = 0;
    virtual void set_brightness(uint8_t brightness) = 0;
    virtual void set_digits(const std::array<uint8_t, 6> &digits) = 0;
    virtual void fade_brightness(uint8_t target, uint16_t duration_ms) = 0;
    virtual void nixie_scan_start(i2c_port_t i2c_port) = 0;
    virtual std::vector<NixieTube *> get_tubes() = 0;
};

// Concrete Implementation
class NixieDriver : public INixieDriver
{
public:
    NixieDriver();
    ~NixieDriver() override = default;

    void display_time(uint8_t h, uint8_t m, uint8_t s) override;
    void display_date(uint8_t yy, uint8_t mm, uint8_t dd) override;
    void display_number(uint32_t number) override;
    void set_brightness(uint8_t brightness) override;
    void set_digits(const std::array<uint8_t, 6> &digits) override;
    void fade_brightness(uint8_t target, uint16_t duration_ms) override;
    void nixie_scan_start(i2c_port_t i2c_port) override;
    std::vector<NixieTube *> get_tubes() override;

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

    struct FadeState
    {
        bool active = false;
        uint8_t target = 0;
        int8_t step = 0;
        uint16_t frames_remaining = 0;
    };

    static void scan_task_entry(void *param);
    void scan_loop();
    void push_all_cathodes(std::array<Pca9685, 4> &pca);
    void flush_chip(Pca9685 &chip, uint8_t chip_index);
    void update_fade_step();

    std::array<NixieTube, 6> tubes_;
    std::array<uint8_t, 6> digit_cache_{};
    std::array<ChannelRef, 6> active_cathode_{};
    std::array<std::array<uint16_t, 16>, 4> cathode_shadow_{};
    std::array<std::array<uint16_t, 16>, 4> flushed_shadow_{};
    std::array<bool, 4> chip_dirty_{};
    uint8_t brightness_ = 0;
    volatile bool pwm_dirty_ = true;
    FadeState fade_{};
    TaskHandle_t scan_task_ = nullptr;
    i2c_port_t i2c_port_ = I2C_NUM_0;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
