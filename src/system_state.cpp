#include "system_state.h"

SystemState::SystemState()
    : mux_(portMUX_INITIALIZER_UNLOCKED),
      gasgauge_{},
      device_info_{}
{
    gasgauge_.valid = false;
    gasgauge_.updated_at = 0;
    device_info_.probed = false;
}

void SystemState::update_gasgauge(const GasgaugeData &data)
{
    portENTER_CRITICAL(&mux_);
    gasgauge_.data = data;
    gasgauge_.valid = true;
    gasgauge_.updated_at = xTaskGetTickCount();
    portEXIT_CRITICAL(&mux_);
}

bool SystemState::get_gasgauge(GasgaugeSnapshot *out) const
{
    if (!out) {
        return false;
    }

    portENTER_CRITICAL(&mux_);
    *out = gasgauge_;
    portEXIT_CRITICAL(&mux_);
    return out->valid;
}

void SystemState::set_device_info(const GasgaugeDeviceInfo &info)
{
    portENTER_CRITICAL(&mux_);
    device_info_ = info;
    portEXIT_CRITICAL(&mux_);
}

bool SystemState::get_device_info(GasgaugeDeviceInfo *out) const
{
    if (!out) {
        return false;
    }

    portENTER_CRITICAL(&mux_);
    *out = device_info_;
    portEXIT_CRITICAL(&mux_);
    return out->probed;
}
