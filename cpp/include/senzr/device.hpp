#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/platform.hpp"
#include <string>
#include <vector>
#include <memory>

namespace senzr {

/**
 * @brief Comprehensive system health and performance telemetry metrics.
 */
struct SystemMetrics {
    double cpu_temp_c{0.0};
    double cpu_usage_percent{0.0};
    uint64_t ram_total_mb{0};
    uint64_t ram_used_mb{0};
    uint64_t disk_total_mb{0};
    uint64_t disk_used_mb{0};
    uint64_t uptime_seconds{0};
};

using Telemetry = SystemMetrics;

/**
 * @brief Complete hardware identification snapshot.
 */
struct DeviceInfo {
    std::string mac_address;
    std::string normalized_mac;
    std::string serial_number;
    std::string board_model;
    std::string firmware_version;
    std::string ip_address;
};

/**
 * @brief Thread-safe Device management and telemetry API.
 */
class Device {
public:
    static Device& get_instance();

    // ── Hardware Identification ───────────────────────────────────────────────
    std::string get_mac_address(const std::string& preferred_iface = "eth0");
    std::string get_normalized_mac();
    std::string get_serial_number();
    std::string get_board_model();
    std::string get_firmware_version();
    std::string get_ip_address();
    Result<DeviceInfo> get_device_info();

    // ── Telemetry & Metrics ───────────────────────────────────────────────────
    Result<SystemMetrics> get_system_metrics();

    // ── Platform & Capabilities ───────────────────────────────────────────────
    Platform get_platform();
    Result<PlatformInfo> get_platform_info();
    bool has_capability(Capability cap);

    // ── System & Service Control ──────────────────────────────────────────────
    Result<void> reboot(int delay_seconds = 3);
    Result<void> shutdown(int delay_seconds = 3);
    Result<void> ensure_service_installed(const std::string& service_name = "edge-gateway");
    Result<void> control_service(const std::string& action, const std::string& service_name = "edge-gateway");

private:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace senzr
