#include "nixie_driver.h"
#include "pca9685/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <algorithm>

namespace
{
constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr gpio_num_t kI2cSda = static_cast<gpio_num_t>(6);
constexpr gpio_num_t kI2cScl = static_cast<gpio_num_t>(5);
constexpr gpio_num_t kPca9685OePin = static_cast<gpio_num_t>(4);
constexpr uint32_t kI2cClockHz = 400000;

constexpr float kPwmFrequencyHz = 200.0f;
constexpr uint32_t kScanFrameHz = 100;
constexpr uint32_t kFramePeriodMs = 1000 / kScanFrameHz;
constexpr uint32_t kStepMs = 1;
constexpr uint32_t kExtraIdleMs = (kFramePeriodMs > (kStepMs * 6))
                                     ? (kFramePeriodMs - (kStepMs * 6))
                                     : 0;

constexpr uint8_t kPcaAddresses[4] = {0x40, 0x41, 0x42, 0x43};

struct ChannelRef
{
    uint8_t chip_index;
    uint8_t channel;
};

std::array<std::array<ChannelRef, 10>, 6> kTubeMap = {};
bool kMapInitialized = false;

void init_default_mapping()
{
    if (kMapInitialized) {
        return;
    }
    // Tube 1: 0x40 ch0-9
    for (uint8_t d = 0; d < 10; ++d) {
        kTubeMap[0][d] = {0, d};
    }
    // Tube 2: 0x40 ch10-15 + 0x41 ch0-3
    for (uint8_t d = 0; d < 6; ++d) {
        kTubeMap[1][d] = {0, static_cast<uint8_t>(10 + d)};
    }
    for (uint8_t d = 6; d < 10; ++d) {
        kTubeMap[1][d] = {1, static_cast<uint8_t>(d - 6)};
    }
    // Tube 3: 0x41 ch4-13
    for (uint8_t d = 0; d < 10; ++d) {
        kTubeMap[2][d] = {1, static_cast<uint8_t>(4 + d)};
    }
    // Tube 4: 0x42 ch0-9
    for (uint8_t d = 0; d < 10; ++d) {
        kTubeMap[3][d] = {2, d};
    }
    // Tube 5: 0x42 ch10-15 + 0x43 ch0-3
    for (uint8_t d = 0; d < 6; ++d) {
        kTubeMap[4][d] = {2, static_cast<uint8_t>(10 + d)};
    }
    for (uint8_t d = 6; d < 10; ++d) {
        kTubeMap[4][d] = {3, static_cast<uint8_t>(d - 6)};
    }
    // Tube 6: 0x43 ch4-13
    for (uint8_t d = 0; d < 10; ++d) {
        kTubeMap[5][d] = {3, static_cast<uint8_t>(4 + d)};
    }

    kMapInitialized = true;
}

} // namespace

static const char *kTag = "NixieDriver";

NixieDriver::NixieDriver()
    : brightness_(128)
{
    // tubes_ is initialized by default constructor
    for (size_t i = 0; i < digit_cache_.size(); ++i) {
        digit_cache_[i] = 0;
    }
}

void NixieDriver::display_time(uint8_t h, uint8_t m, uint8_t s)
{
    if (tubes_.size() >= 6) {
        digit_cache_[0] = h / 10;
        digit_cache_[1] = h % 10;
        digit_cache_[2] = m / 10;
        digit_cache_[3] = m % 10;
        digit_cache_[4] = s / 10;
        digit_cache_[5] = s % 10;
        for (size_t i = 0; i < tubes_.size(); ++i) {
            tubes_[i].set_numeral(digit_cache_[i]);
        }
    }
}

void NixieDriver::display_number(uint32_t number)
{
    for (int i = static_cast<int>(tubes_.size()) - 1; i >= 0; --i) {
        digit_cache_[static_cast<size_t>(i)] = number % 10;
        tubes_[static_cast<size_t>(i)].set_numeral(digit_cache_[static_cast<size_t>(i)]);
        number /= 10;
    }
}

void NixieDriver::set_brightness(uint8_t brightness)
{
    brightness_ = brightness;
}

void NixieDriver::set_digits(const std::array<uint8_t, 6> &digits)
{
    digit_cache_ = digits;
    for (size_t i = 0; i < tubes_.size(); ++i) {
        tubes_[i].set_numeral(digit_cache_[i]);
    }
}

void NixieDriver::nixie_scan_start()
{
    if (scan_task_) {
        return;
    }
    init_default_mapping();
    xTaskCreate(scan_task_entry, "nixie_scan", 4096, this, 6, &scan_task_);
}

std::vector<NixieTube *> NixieDriver::get_tubes()
{
    std::vector<NixieTube *> ptrs;
    ptrs.reserve(tubes_.size());
    for (auto &tube : tubes_) {
        ptrs.push_back(&tube);
    }
    return ptrs;
}

void NixieDriver::scan_task_entry(void *param)
{
    auto *driver = static_cast<NixieDriver *>(param);
    driver->scan_loop();
}

void NixieDriver::scan_loop()
{
    if (!Pca9685::init_i2c(kI2cPort, kI2cSda, kI2cScl, kI2cClockHz)) {
        ESP_LOGE(kTag, "Failed to init I2C");
        vTaskDelete(nullptr);
        return;
    }

    std::array<Pca9685, 4> pca = {
        Pca9685(kI2cPort, kPcaAddresses[0]),
        Pca9685(kI2cPort, kPcaAddresses[1]),
        Pca9685(kI2cPort, kPcaAddresses[2]),
        Pca9685(kI2cPort, kPcaAddresses[3])
    };

    for (auto &chip : pca) {
        if (!chip.init(kI2cClockHz, kPwmFrequencyHz)) {
            ESP_LOGE(kTag, "Failed to init PCA9685");
            vTaskDelete(nullptr);
            return;
        }
        chip.set_all_off();
    }

    // Enable PCA9685 outputs
    gpio_set_level(kPca9685OePin, 0);

    size_t tube_index = 0;
    while (true) {
        const uint8_t numeral = digit_cache_[tube_index] % 10;
        const uint16_t duty = static_cast<uint16_t>((static_cast<uint32_t>(brightness_) * 4095) / 255);

        for (auto &chip : pca) {
            chip.set_all_off();
        }
        apply_tube_output(pca, tube_index, numeral, duty);

        tube_index = (tube_index + 1) % tubes_.size();
        vTaskDelay(pdMS_TO_TICKS(kStepMs));
        if (tube_index == 0 && kExtraIdleMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(kExtraIdleMs));
        }
    }
}

void NixieDriver::apply_tube_output(std::array<Pca9685, 4> &pca,
                                    size_t tube_index,
                                    uint8_t numeral,
                                    uint16_t duty)
{
    if (tube_index >= kTubeMap.size() || numeral >= 10) {
        return;
    }
    const ChannelRef ref = kTubeMap[tube_index][numeral];
    pca[ref.chip_index].set_duty(ref.channel, duty);
}
