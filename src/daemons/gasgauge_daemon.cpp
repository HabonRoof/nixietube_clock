#include "daemons/gasgauge_daemon.h"
#include "debug_session_log.h"
#include "esp_log.h"

static const char *TAG = "GasgaugeDaemon";

GasgaugeDaemon::GasgaugeDaemon(IGasgaugeDriver &driver, SystemState &system_state)
    : driver_(driver),
      system_state_(system_state),
      task_handle_(nullptr)
{
}

GasgaugeDaemon::~GasgaugeDaemon()
{
    if (task_handle_) {
        vTaskDelete(task_handle_);
    }
}

void GasgaugeDaemon::start()
{
    xTaskCreate(task_entry, "gasgauge_daemon", 4096, this, 5, &task_handle_);
}

bool GasgaugeDaemon::probe_device_info(GasgaugeDeviceInfo &info)
{
    if (!driver_.probe(info)) {
        return false;
    }
    system_state_.set_device_info(info);
    return true;
}

bool GasgaugeDaemon::configure_capacity(uint16_t mah, bool force)
{
    if (!driver_.configure(mah, force)) {
        return false;
    }

    GasgaugeDeviceInfo info;
    if (driver_.probe(info)) {
        system_state_.set_device_info(info);
    }
    return true;
}

bool GasgaugeDaemon::read_live(GasgaugeData &data)
{
    if (!driver_.get_data(data)) {
        return false;
    }
    system_state_.update_gasgauge(data);
    return true;
}

bool GasgaugeDaemon::get_cached_snapshot(GasgaugeSnapshot &snapshot) const
{
    return system_state_.get_gasgauge(&snapshot);
}

bool GasgaugeDaemon::peek_registers(uint8_t reg, uint8_t *out, size_t len)
{
    return driver_.peek_registers(reg, out, len);
}

bool GasgaugeDaemon::dump_state_block(uint8_t class_id, uint8_t block_index,
                                      uint8_t out[32], uint8_t *checksum)
{
    return driver_.dump_state_block(class_id, block_index, out, checksum);
}

void GasgaugeDaemon::task_entry(void *param)
{
    auto *daemon = static_cast<GasgaugeDaemon *>(param);
    daemon->loop();
}

bool GasgaugeDaemon::ensure_probed()
{
    GasgaugeDeviceInfo info;
    return probe_device_info(info);
}

void GasgaugeDaemon::loop()
{
    ESP_LOGI(TAG, "Gasgauge Daemon Started");

    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t poll_interval = pdMS_TO_TICKS(kPollIntervalMs);
    const TickType_t retry_interval = pdMS_TO_TICKS(kRetryIntervalMs);

    bool init_ok = false;
    uint8_t read_failures = 0;
    uint32_t probe_attempts = 0;

    if (ensure_probed()) {
        probe_attempts = 1;
        init_ok = driver_.is_ready();
        if (!init_ok) {
            ESP_LOGW(TAG,
                     "Probed on attempt 1 but gauging not ready; retrying every %u s",
                     static_cast<unsigned>(kRetryIntervalMs / 1000));
        }
    } else {
        ESP_LOGW(TAG, "Gasgauge unavailable (probe attempt 1 failed); retrying every %u s",
                 static_cast<unsigned>(kRetryIntervalMs / 1000));
        probe_attempts = 1;
    }

    if (init_ok) {
        ESP_LOGI(TAG, "Gasgauge ready");
        last_wake_time = xTaskGetTickCount();
    }

    while (true) {
        if (!init_ok) {
            vTaskDelay(retry_interval);
            probe_attempts++;
            if (ensure_probed()) {
                init_ok = driver_.is_ready();
                if (init_ok) {
                    ESP_LOGI(TAG, "Gasgauge ready on probe attempt %u", probe_attempts);
                    read_failures = 0;
                    last_wake_time = xTaskGetTickCount();
                } else {
                    ESP_LOGW(TAG,
                             "Probed on attempt %u but gauging not ready",
                             probe_attempts);
                }
            } else {
                ESP_LOGW(TAG, "Probe attempt %u failed", probe_attempts);
            }
            continue;
        }

        GasgaugeData data;
        if (read_live(data)) {
            read_failures = 0;
            ESP_LOGD(TAG, "Battery: %u%%, %u mV, %d mA, SOH %u%%",
                     data.soc, data.voltage_mv, data.current_ma, data.soh);
        } else {
            read_failures++;
            // #region agent log
            dbg_session_log("C", "gasgauge_daemon.cpp:loop", "read_live_fail",
                            static_cast<int32_t>(read_failures),
                            static_cast<int32_t>(kMaxReadFailures), 0);
            // #endregion
            if (read_failures >= kMaxReadFailures) {
                ESP_LOGW(TAG, "Gasgauge read failed %u times; will retry probe in %u s",
                         read_failures,
                         static_cast<unsigned>(kRetryIntervalMs / 1000));
                init_ok = false;
                read_failures = 0;
            } else {
                ESP_LOGW(TAG, "Gasgauge read failed (%u/%u); retrying next poll",
                         read_failures, kMaxReadFailures);
            }
        }

        vTaskDelayUntil(&last_wake_time, poll_interval);
    }
}
