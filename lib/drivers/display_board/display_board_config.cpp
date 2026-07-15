#include "display_board_config.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "hal/adc_types.h"
#include "soc/adc_channel.h"

namespace
{
constexpr const char *kTag = "DisplayBoard";

// GPIO19 on ESP32-S3 is ADC2 channel 8.
constexpr adc_unit_t kDisplayTypeAdcUnit = ADC_UNIT_2;
constexpr adc_channel_t kDisplayTypeAdcChannel = ADC_CHANNEL_8;

// Thresholds sit midway between the three strap voltages (0 V, 1.65 V, 3.3 V).
constexpr int kThresholdLowMv = 825;
constexpr int kThresholdHighMv = 2475;
constexpr int kAdcSampleCount = 8;

DisplayBoardType g_display_board_type = kDefaultDisplayBoardType;

constexpr DisplayBoardProfile kProfileIn4_6 = {
    DisplayBoardType::IN4_6,
    "IN-4 (6)",
    6,
    4,
    {0x40, 0x41, 0x42, 0x43, 0},
};

constexpr DisplayBoardProfile kProfileIn14_8 = {
    DisplayBoardType::IN14_8,
    "IN-14 (8)",
    8,
    5,
    {0x40, 0x41, 0x42, 0x43, 0x44},
};

constexpr DisplayBoardProfile kProfileIn14_6 = {
    DisplayBoardType::IN14_6,
    "IN-14 (6)",
    6,
    4,
    {0x40, 0x41, 0x42, 0x43, 0},
};

const DisplayBoardProfile &profile_for_type(DisplayBoardType type)
{
    switch (type) {
    case DisplayBoardType::IN14_8:
        return kProfileIn14_8;
    case DisplayBoardType::IN14_6:
        return kProfileIn14_6;
    case DisplayBoardType::IN4_6:
    default:
        return kProfileIn4_6;
    }
}

DisplayBoardType classify_display_type_mv(int voltage_mv)
{
    if (voltage_mv < kThresholdLowMv) {
        return DisplayBoardType::IN4_6;
    }
    if (voltage_mv < kThresholdHighMv) {
        return DisplayBoardType::IN14_8;
    }
    return DisplayBoardType::IN14_6;
}

adc_cali_handle_t create_adc_cali_handle(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten)
{
    adc_cali_handle_t handle = nullptr;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &handle) == ESP_OK) {
        return handle;
    }
    return nullptr;
}

int read_display_type_voltage_mv()
{
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = kDisplayTypeAdcUnit,
        .clk_src = static_cast<adc_oneshot_clk_src_t>(0),
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &adc_handle) != ESP_OK) {
        ESP_LOGW(kTag, "Failed to create ADC unit, defaulting to IN-4 (6)");
        return 0;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(adc_handle, kDisplayTypeAdcChannel, &chan_cfg) != ESP_OK) {
        ESP_LOGW(kTag, "Failed to configure ADC channel, defaulting to IN-4 (6)");
        adc_oneshot_del_unit(adc_handle);
        return 0;
    }

    adc_cali_handle_t cali_handle =
        create_adc_cali_handle(kDisplayTypeAdcUnit, kDisplayTypeAdcChannel, ADC_ATTEN_DB_12);

    int total_mv = 0;
    int valid_samples = 0;
    for (int i = 0; i < kAdcSampleCount; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, kDisplayTypeAdcChannel, &raw) != ESP_OK) {
            continue;
        }

        int voltage_mv = 0;
        if (cali_handle != nullptr &&
            adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv) == ESP_OK) {
            total_mv += voltage_mv;
            ++valid_samples;
        }
    }

    if (cali_handle != nullptr) {
        adc_cali_delete_scheme_curve_fitting(cali_handle);
    }
    adc_oneshot_del_unit(adc_handle);

    if (valid_samples == 0) {
        ESP_LOGW(kTag, "No valid ADC samples, defaulting to IN-4 (6)");
        return 0;
    }

    return total_mv / valid_samples;
}
} // namespace

const char *display_board_type_name(DisplayBoardType type)
{
    return profile_for_type(type).name;
}

void init_display_type_adc()
{
    const int voltage_mv = read_display_type_voltage_mv();
    g_display_board_type = classify_display_type_mv(voltage_mv);

    ESP_LOGI(kTag,
             "Display board type: %s (GPIO19 %d mV)",
             display_board_type_name(g_display_board_type),
             voltage_mv);
}

DisplayBoardType get_display_board_type()
{
    return g_display_board_type;
}

const DisplayBoardProfile &get_display_board_profile()
{
    return profile_for_type(g_display_board_type);
}
