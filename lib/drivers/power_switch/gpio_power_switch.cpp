#include "gpio_power_switch.h"

#include "esp_log.h"

static const char *TAG = "GpioPowerSwitch";

GpioPowerSwitch::GpioPowerSwitch(gpio_num_t hv_pin, gpio_num_t dfplayer_pin)
    : hv_pin_(hv_pin),
      dfplayer_pin_(dfplayer_pin),
      hv_enabled_(false),
      dfplayer_enabled_(false)
{
}

bool GpioPowerSwitch::init()
{
    gpio_config_t conf = {};
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.mode = GPIO_MODE_OUTPUT;
    conf.pin_bit_mask = (1ULL << hv_pin_) | (1ULL << dfplayer_pin_);
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.pull_up_en = GPIO_PULLUP_DISABLE;

    if (gpio_config(&conf) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power switch GPIOs");
        return false;
    }

    hv_enabled_ = false;
    dfplayer_enabled_ = false;
    gpio_set_level(hv_pin_, 0);
    gpio_set_level(dfplayer_pin_, 0);

    ESP_LOGI(TAG, "Power switches initialized (HV GPIO%d, DF GPIO%d, both OFF)",
             static_cast<int>(hv_pin_), static_cast<int>(dfplayer_pin_));
    return true;
}

bool GpioPowerSwitch::set_hv_enabled(bool enabled)
{
    gpio_set_level(hv_pin_, enabled ? 1 : 0);
    hv_enabled_ = enabled;
    ESP_LOGI(TAG, "HV rail %s", enabled ? "enabled" : "disabled");
    return true;
}

bool GpioPowerSwitch::set_dfplayer_enabled(bool enabled)
{
    gpio_set_level(dfplayer_pin_, enabled ? 1 : 0);
    dfplayer_enabled_ = enabled;
    ESP_LOGI(TAG, "DFPlayer rail %s", enabled ? "enabled" : "disabled");
    return true;
}

bool GpioPowerSwitch::get_state(PowerSwitchState &state) const
{
    state.hv_enabled = hv_enabled_;
    state.dfplayer_enabled = dfplayer_enabled_;
    return true;
}
