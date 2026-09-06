#pragma once

#include <cstdint>
#include "driver/i2c.h"

struct Ltr303Sample
{
    uint16_t ch0;
    uint16_t ch1;
    float ratio;
    float lux;
};

enum class Ltr303Gain : uint8_t
{
    X1 = 0,
    X2 = 1,
    X4 = 2,
    X8 = 3,
    X48 = 4,
    X96 = 5,
};

class Ltr303
{
public:
    static constexpr uint16_t kSaturationThreshold = 60000;

    explicit Ltr303(i2c_port_t port, uint8_t address = 0x29);

    bool init();
    bool is_ready() const { return ready_; }
    bool read_raw_lux(float *lux_out);
    bool read_channels(Ltr303Sample *sample_out);
    bool set_gain(Ltr303Gain gain);
    Ltr303Gain get_gain() const { return gain_; }
    static bool is_saturated(const Ltr303Sample &sample);
    static const char *gain_label(Ltr303Gain gain);

private:
    bool read_register(uint8_t reg, uint8_t *val);
    bool write_register(uint8_t reg, uint8_t val);
    bool read_registers(uint8_t reg, uint8_t *data, size_t len);
    float compute_lux(uint16_t ch0, uint16_t ch1) const;
    bool write_als_control();

    i2c_port_t port_;
    uint8_t address_;
    Ltr303Gain gain_ = Ltr303Gain::X1;
    bool ready_ = false;
};

inline Ltr303Gain ltr303_gain_from_storage(uint8_t stored)
{
    if (stored > static_cast<uint8_t>(Ltr303Gain::X96)) {
        return Ltr303Gain::X1;
    }
    return static_cast<Ltr303Gain>(stored);
}

inline uint8_t ltr303_gain_to_storage(Ltr303Gain gain)
{
    return static_cast<uint8_t>(gain);
}
