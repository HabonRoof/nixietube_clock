#include "display_board_config.h"
#include "esp_log.h"

namespace
{
constexpr const char *kTag = "DisplayBoard";
DisplayBoardType g_display_board_type = kDefaultDisplayBoardType;
} // namespace

const char *display_board_type_name(DisplayBoardType type)
{
    switch (type) {
    case DisplayBoardType::IN14:
        return "IN-14";
    case DisplayBoardType::IN4:
    default:
        return "IN-4";
    }
}

void init_display_type_gpio()
{
    gpio_config_t cfg = {};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << kDisplayTypePin);
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));

    g_display_board_type =
        gpio_get_level(kDisplayTypePin) ? DisplayBoardType::IN14 : DisplayBoardType::IN4;

    ESP_LOGI(kTag, "Display board type: %s (GPIO%d %s)",
             display_board_type_name(g_display_board_type),
             static_cast<int>(kDisplayTypePin),
             gpio_get_level(kDisplayTypePin) ? "high" : "low");
}

DisplayBoardType get_display_board_type()
{
    return g_display_board_type;
}
