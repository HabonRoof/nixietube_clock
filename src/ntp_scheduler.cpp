#include "ntp_scheduler.h"
#include "esp_log.h"
#include <ctime>

namespace {
constexpr const char *kTag = "NtpScheduler";
} // namespace

NtpScheduler::NtpScheduler(WifiManager &wifi_manager, SystemState &system_state)
    : wifi_manager_(wifi_manager),
      system_state_(system_state),
      task_handle_(nullptr),
      last_run_yday_(-1)
{
}

NtpScheduler::~NtpScheduler()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void NtpScheduler::start()
{
    if (!task_handle_) {
        xTaskCreate(task_entry, "ntp_sched", 4096, this, 3, &task_handle_);
    }
}

void NtpScheduler::task_entry(void *param)
{
    static_cast<NtpScheduler *>(param)->loop();
}

bool NtpScheduler::should_run_today() const
{
    ClockSettings settings{};
    system_state_.get_settings(&settings);

    time_t now = 0;
    time(&now);
    const time_t local_epoch = now + static_cast<time_t>(settings.tz_offset_hours) * 3600;
    struct tm local_tm = {};
    gmtime_r(&local_epoch, &local_tm);

    if (local_tm.tm_yday == last_run_yday_) {
        return false;
    }

    const int local_hour = local_tm.tm_hour;
    const int local_min = local_tm.tm_min;
    if (local_hour < kNtpLocalHour) {
        return false;
    }
    if (local_hour == kNtpLocalHour && local_min < kNtpLocalMinute) {
        return false;
    }

    WifiStaCredentials creds = {};
    if (!wifi_manager_.load_sta_credentials(&creds) || !creds.configured) {
        return false;
    }

    if (wifi_manager_.is_config_active()) {
        return false;
    }

    return true;
}

void NtpScheduler::loop()
{
    ESP_LOGI(kTag, "NTP scheduler started (daily %02u:%02u local)", kNtpLocalHour, kNtpLocalMinute);

    while (true) {
        if (should_run_today()) {
            time_t now = 0;
            time(&now);
            struct tm local_tm = {};
            localtime_r(&now, &local_tm);
            last_run_yday_ = local_tm.tm_yday;

            ESP_LOGI(kTag, "Triggering scheduled NTP sync");
            wifi_manager_.request_ntp_sync();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
