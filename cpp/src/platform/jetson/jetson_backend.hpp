#pragma once

#include "senzr/platform.hpp"
#include "senzr/device.hpp"

namespace senzr::platform::jetson {

bool is_jetson_platform();
PlatformInfo get_jetson_info();
bool has_jetson_capability(Capability cap);
SystemMetrics get_jetson_metrics();
int map_jetson_pin(int pin_number);

} // namespace senzr::platform::jetson
