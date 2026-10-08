#pragma once

#include "senzr/platform.hpp"
#include "senzr/device.hpp"

namespace senzr::platform::rockchip {

bool is_rk3588_platform();
PlatformInfo get_rk3588_info();
bool has_rk3588_capability(Capability cap);
SystemMetrics get_rk3588_metrics();
int map_rk3588_pin(int pin_number);

} // namespace senzr::platform::rockchip
