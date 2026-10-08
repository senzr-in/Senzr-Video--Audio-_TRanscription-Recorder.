#pragma once

#include "senzr/platform.hpp"
#include "senzr/device.hpp"

namespace senzr::platform {

/**
 * @brief Auto-detect the running host hardware platform.
 */
Platform detect_host_platform();

/**
 * @brief Retrieve complete platform details and accelerator info for the active platform.
 */
PlatformInfo query_platform_info();

/**
 * @brief Query whether a specific hardware capability is available on the current platform.
 */
bool query_capability(Capability cap);

/**
 * @brief Read hardware system metrics (CPU temp, RAM, Disk, Uptime) for current platform.
 */
SystemMetrics query_system_metrics();

/**
 * @brief Map a standard 40-pin header pin number (1..40) to platform-specific Linux GPIO line number.
 */
int map_logical_pin_to_sysfs(int pin_number);

} // namespace senzr::platform
