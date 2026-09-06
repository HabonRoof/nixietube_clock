#pragma once

#include "wifi_manager.h"
#include "system_state.h"

class SystemController;

class NtpScheduler
{
public:
    NtpScheduler(WifiManager &wifi_manager, SystemState &system_state);
    ~NtpScheduler();

    void start();

private:
    static void task_entry(void *param);
    void loop();
    bool should_run_today() const;

    WifiManager &wifi_manager_;
    SystemState &system_state_;
    TaskHandle_t task_handle_;
    int last_run_yday_;

    static constexpr uint8_t kNtpLocalHour = 6;
    static constexpr uint8_t kNtpLocalMinute = 0;
};
