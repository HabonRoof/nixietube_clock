#include "nixie_driver.h"
#include "pca9685/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include <algorithm>

namespace
{
constexpr gpio_num_t kPca9685OePin = static_cast<gpio_num_t>(4);

constexpr gpio_num_t kAnodeA0 = GPIO_NUM_9;
constexpr gpio_num_t kAnodeA1 = GPIO_NUM_10;
constexpr gpio_num_t kAnodeA2 = GPIO_NUM_11;
constexpr uint8_t kAnodeBlankAddress = 7;

constexpr float kPwmFrequencyHz = 200.0f;
constexpr uint32_t kAnodeScanHz = 1000;
constexpr uint32_t kAnodeTubeCount = 6;
constexpr uint32_t kAnodeFrameUs = 1000000 / kAnodeScanHz;
constexpr uint32_t kAnodeSlotUs = kAnodeFrameUs / kAnodeTubeCount;

constexpr uint32_t kPwmUpdateHz = 100;
constexpr uint32_t kPwmUpdatePeriodMs = 1000 / kPwmUpdateHz;

constexpr uint8_t kPcaAddresses[4] = {0x40, 0x41, 0x42, 0x43};

struct AnodeScanCtx
{
    size_t tube_index = 0;
};

AnodeScanCtx g_anode_ctx;
esp_timer_handle_t g_anode_timer = nullptr;
portMUX_TYPE g_anode_mux = portMUX_INITIALIZER_UNLOCKED;

constexpr uint32_t kAnodePinMask =
    (1U << kAnodeA0) | (1U << kAnodeA1) | (1U << kAnodeA2);

void select_anode(uint8_t address);

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
    // 60 cathodes (6 tubes x 10 digits) are packed sequentially across 4 PCA9685
    // chips (16 channels each): global_index = tube * 10 + digit.
    for (uint8_t tube = 0; tube < 6; ++tube) {
        for (uint8_t digit = 0; digit < 10; ++digit) {
            const uint8_t global = static_cast<uint8_t>(tube * 10 + digit);
            kTubeMap[tube][digit] = {static_cast<uint8_t>(global / 16),
                                     static_cast<uint8_t>(global % 16)};
        }
    }

    kMapInitialized = true;
}

void init_anode_mux()
{
    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = (1ULL << kAnodeA0) | (1ULL << kAnodeA1) | (1ULL << kAnodeA2);
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);
    select_anode(kAnodeBlankAddress);
}

void select_anode(uint8_t address)
{
    const uint32_t levels = ((address & 1U) << kAnodeA0) | (((address >> 1) & 1U) << kAnodeA1) |
                            (((address >> 2) & 1U) << kAnodeA2);
    portENTER_CRITICAL(&g_anode_mux);
    const uint32_t out = REG_READ(GPIO_OUT_REG);
    REG_WRITE(GPIO_OUT_REG, (out & ~kAnodePinMask) | levels);
    portEXIT_CRITICAL(&g_anode_mux);
}

void anode_timer_callback(void *arg)
{
    auto *ctx = static_cast<AnodeScanCtx *>(arg);
    gpio_set_level(kPca9685OePin, 1);
    select_anode(kAnodeBlankAddress);
    select_anode(static_cast<uint8_t>(ctx->tube_index));
    gpio_set_level(kPca9685OePin, 0);
    ctx->tube_index = (ctx->tube_index + 1) % kAnodeTubeCount;
}

} // namespace

static const char *kTag = "NixieDriver";

NixieDriver::NixieDriver()
    : brightness_(255)
{
    for (size_t i = 0; i < digit_cache_.size(); ++i) {
        digit_cache_[i] = 0;
        tube_brightness_[i] = 255;
    }
    for (auto &ref : active_cathode_) {
        ref = {255, 255};
    }
}

void NixieDriver::display_time(uint8_t h, uint8_t m, uint8_t s)
{
    portENTER_CRITICAL(&mux_);
    digit_cache_[0] = h / 10;
    digit_cache_[1] = h % 10;
    digit_cache_[2] = m / 10;
    digit_cache_[3] = m % 10;
    digit_cache_[4] = s / 10;
    digit_cache_[5] = s % 10;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::display_date(uint8_t yy, uint8_t mm, uint8_t dd)
{
    portENTER_CRITICAL(&mux_);
    digit_cache_[0] = yy / 10;
    digit_cache_[1] = yy % 10;
    digit_cache_[2] = mm / 10;
    digit_cache_[3] = mm % 10;
    digit_cache_[4] = dd / 10;
    digit_cache_[5] = dd % 10;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::display_number(uint32_t number)
{
    portENTER_CRITICAL(&mux_);
    for (int i = static_cast<int>(kTubeCount) - 1; i >= 0; --i) {
        digit_cache_[static_cast<size_t>(i)] = number % 10;
        number /= 10;
    }
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::set_brightness(uint8_t brightness)
{
    portENTER_CRITICAL(&mux_);
    brightness_ = brightness;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::set_digits(const std::array<uint8_t, 6> &digits)
{
    portENTER_CRITICAL(&mux_);
    digit_cache_ = digits;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::set_digit_at(size_t tube_index, uint8_t digit)
{
    if (tube_index >= digit_cache_.size()) {
        return;
    }

    portENTER_CRITICAL(&mux_);
    digit_cache_[tube_index] = digit % 10;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::set_tube_brightness(size_t tube_index, uint8_t scale)
{
    if (tube_index >= tube_brightness_.size()) {
        return;
    }

    portENTER_CRITICAL(&mux_);
    tube_brightness_[tube_index] = scale;
    pwm_dirty_ = true;
    portEXIT_CRITICAL(&mux_);
}

void NixieDriver::nixie_scan_start(i2c_port_t i2c_port)
{
    if (scan_task_) {
        return;
    }
    i2c_port_ = i2c_port;
    init_default_mapping();
    xTaskCreate(scan_task_entry, "nixie_scan", 4096, this, 6, &scan_task_);
}

void NixieDriver::scan_task_entry(void *param)
{
    auto *driver = static_cast<NixieDriver *>(param);
    driver->scan_loop();
}

void NixieDriver::flush_chip(Pca9685 &chip, uint8_t chip_index)
{
    auto &shadow = cathode_shadow_[chip_index];
    auto &flushed = flushed_shadow_[chip_index];

    uint8_t ch = 0;
    while (ch < 16) {
        while (ch < 16 && shadow[ch] == flushed[ch]) {
            ++ch;
        }
        if (ch >= 16) {
            break;
        }

        const uint8_t start = ch;
        while (ch < 16 && shadow[ch] != flushed[ch]) {
            ++ch;
        }
        const uint8_t count = static_cast<uint8_t>(ch - start);

        uint16_t duties[16] = {};
        for (uint8_t i = 0; i < count; ++i) {
            duties[i] = shadow[start + i];
        }

        if (!chip.set_pwm_block(start, duties, count)) {
            ESP_LOGW(kTag, "Failed to flush PCA9685 chip %u ch %u", chip_index, start);
        }

        for (uint8_t i = 0; i < count; ++i) {
            flushed[start + i] = shadow[start + i];
        }
    }
}

void NixieDriver::push_all_cathodes(std::array<Pca9685, 4> &pca)
{
    portENTER_CRITICAL(&mux_);
    const std::array<uint8_t, 6> digits = digit_cache_;
    const std::array<uint8_t, 6> tube_scales = tube_brightness_;
    const uint8_t global_brightness = brightness_;
    portEXIT_CRITICAL(&mux_);

    for (size_t t = 0; t < kTubeCount; ++t) {
        const uint16_t duty = static_cast<uint16_t>(
            (static_cast<uint32_t>(global_brightness) * tube_scales[t] * 4095) / (255 * 255));
        const auto mapped = kTubeMap[t][digits[t] % 10];
        const ChannelRef new_ref = {mapped.chip_index, mapped.channel};
        const ChannelRef old_ref = active_cathode_[t];

        if (old_ref.chip_index < 4 &&
            (old_ref.chip_index != new_ref.chip_index || old_ref.channel != new_ref.channel)) {
            cathode_shadow_[old_ref.chip_index][old_ref.channel] = 0;
            chip_dirty_[old_ref.chip_index] = true;
        }

        if (old_ref.chip_index >= 4 ||
            old_ref.chip_index != new_ref.chip_index ||
            old_ref.channel != new_ref.channel ||
            cathode_shadow_[new_ref.chip_index][new_ref.channel] != duty) {
            cathode_shadow_[new_ref.chip_index][new_ref.channel] = duty;
            chip_dirty_[new_ref.chip_index] = true;
        }

        active_cathode_[t] = new_ref;
    }

    for (size_t i = 0; i < pca.size(); ++i) {
        if (chip_dirty_[i]) {
            flush_chip(pca[i], static_cast<uint8_t>(i));
            chip_dirty_[i] = false;
        }
    }
}

void NixieDriver::scan_loop()
{
    std::array<Pca9685, 4> pca = {
        Pca9685(i2c_port_, kPcaAddresses[0]),
        Pca9685(i2c_port_, kPcaAddresses[1]),
        Pca9685(i2c_port_, kPcaAddresses[2]),
        Pca9685(i2c_port_, kPcaAddresses[3])
    };

    for (auto &chip : pca) {
        if (!chip.init(kPwmFrequencyHz)) {
            ESP_LOGE(kTag, "Failed to init PCA9685");
            vTaskDelete(nullptr);
            return;
        }
        if (!chip.zero_all_channels()) {
            ESP_LOGE(kTag, "Failed to zero PCA9685 outputs");
            vTaskDelete(nullptr);
            return;
        }
    }

    gpio_set_level(kPca9685OePin, 0);
    init_anode_mux();

    g_anode_ctx.tube_index = 0;
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = anode_timer_callback;
    timer_args.arg = &g_anode_ctx;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "anode_mux";
    if (esp_timer_create(&timer_args, &g_anode_timer) != ESP_OK ||
        esp_timer_start_periodic(g_anode_timer, kAnodeSlotUs) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to start anode mux timer");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kPwmUpdatePeriodMs));

        bool dirty = false;

        portENTER_CRITICAL(&mux_);
        dirty = pwm_dirty_;
        portEXIT_CRITICAL(&mux_);

        if (dirty) {
            push_all_cathodes(pca);
            portENTER_CRITICAL(&mux_);
            pwm_dirty_ = false;
            portEXIT_CRITICAL(&mux_);
        }
    }
}
