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
    virtual void display_number(uint32_t number) = 0;
    virtual void set_brightness(uint8_t brightness) = 0; // PWM or similar if supported
    virtual void set_digits(const std::array<uint8_t, 6> &digits) = 0;
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
    void display_number(uint32_t number) override;
    void set_brightness(uint8_t brightness) override;
    void set_digits(const std::array<uint8_t, 6> &digits) override;
    void nixie_scan_start(i2c_port_t i2c_port) override;
    std::vector<NixieTube *> get_tubes() override;

private:
    static void scan_task_entry(void *param);
    void scan_loop();
    void apply_tube_output(std::array<Pca9685, 4> &pca,
                           size_t tube_index,
                           uint8_t numeral,
                           uint16_t duty);

    std::array<NixieTube, 6> tubes_;
    std::array<uint8_t, 6> digit_cache_{};
    uint8_t brightness_ = 0;
    TaskHandle_t scan_task_ = nullptr;
    i2c_port_t i2c_port_ = I2C_NUM_0;
};
