#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include "senzr/platform.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace senzr {
inline namespace v1 {

/**
 * @brief Comprehensive system health and performance telemetry metrics.
 */
struct SENZR_API SystemMetrics {
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
struct SENZR_API DeviceInfo {
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
class SENZR_API Device {
public:
    static Device& get_instance();

    // ── Hardware Identification ───────────────────────────────────────────────
    [[nodiscard]] std::string get_mac_address(const std::string& preferred_iface = "eth0");
    [[nodiscard]] std::string get_normalized_mac();
    [[nodiscard]] std::string get_serial_number();
    [[nodiscard]] std::string get_board_model();
    [[nodiscard]] std::string get_firmware_version();
    [[nodiscard]] std::string get_ip_address();
    [[nodiscard]] Result<DeviceInfo> get_device_info();

    // ── Telemetry & Metrics ───────────────────────────────────────────────────
    [[nodiscard]] Result<SystemMetrics> get_system_metrics();

    // ── Platform & Capabilities ───────────────────────────────────────────────
    [[nodiscard]] Platform get_platform();
    [[nodiscard]] Result<PlatformInfo> get_platform_info();
    [[nodiscard]] bool has_capability(Capability cap);

    // ── System & Service Control ──────────────────────────────────────────────
    [[nodiscard]] Result<void> reboot(int delay_seconds = 3);
    [[nodiscard]] Result<void> shutdown(int delay_seconds = 3);
    [[nodiscard]] Result<void> ensure_service_installed(const std::string& service_name = "edge-gateway");
    [[nodiscard]] Result<void> control_service(const std::string& action, const std::string& service_name = "edge-gateway");

    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;

private:
    Device();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace v1
} // namespace senzr
