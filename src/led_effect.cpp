#include "led_effect.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *kLogTag = "LedEffect";
static constexpr TickType_t kFrameDelayTicks = pdMS_TO_TICKS(10); // 10ms

DefaultLedEffect::DefaultLedEffect(ILedStrip &strip, NixieDigit *digits, std::size_t digit_count)
    : strip_(strip), digits_(digits), digit_count_(digit_count)
{
}

void DefaultLedEffect::loop()
{
    ESP_LOGI(kLogTag, "Starting LED effect loop");
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true)
    {
        // 10ms frame time
        // Calculate dt if needed, or assume fixed step
        // For simplicity, let's assume 10ms fixed step for animations as per original logic
        
        for (std::size_t i = 0; i < digit_count_; ++i)
        {
            digits_[i].update(10); // Update with 10ms dt
        }
        
        strip_.show();
        
        vTaskDelayUntil(&last_wake_time, kFrameDelayTicks);
    }
}
