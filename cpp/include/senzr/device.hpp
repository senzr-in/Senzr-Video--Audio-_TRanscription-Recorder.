#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace senzr {

struct SystemMetrics {
    double cpu_temp_c{0.0};
    double cpu_usage_percent{0.0};
    uint64_t ram_total_mb{0};
    uint64_t ram_used_mb{0};
    uint64_t disk_total_mb{0};
    uint64_t disk_used_mb{0};
    uint64_t uptime_seconds{0};
};

class Device {
public:
    static Device& get_instance();

    // Hardware Identification
    std::string get_mac_address(const std::string& preferred_iface = "eth0");
    std::string get_normalized_mac(); // Lowercase hex without colons
    std::string get_serial_number();
    std::string get_board_model();
    std::string get_firmware_version();

    // Network & Telemetry
    std::string get_ip_address();
    SystemMetrics get_system_metrics();
    nlohmann::json get_telemetry_json();

    // System Control
    bool reboot(int delay_seconds = 3);
    bool shutdown(int delay_seconds = 3);

private:
    Device() = default;
    std::string exec_cmd(const std::string& cmd);
    std::string cached_mac_{""};
    std::string cached_serial_{""};
};

} // namespace senzr
