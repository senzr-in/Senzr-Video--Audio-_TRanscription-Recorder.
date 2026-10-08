#include <senzr/senzr.hpp>
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== Example 01: Device Hardware & Telemetry ===\n";
    std::cout << "SDK Version: " << senzr::version_string() << "\n\n";

    auto& dev = senzr::Device::get_instance();
    auto info = dev.get_device_info();
    if (info) {
        std::cout << "Hardware Identification:\n"
                  << "  MAC Address   : " << info.value().mac_address << "\n"
                  << "  Serial Number : " << info.value().serial_number << "\n"
                  << "  Board Model   : " << info.value().board_model << "\n"
                  << "  Firmware Ver  : " << info.value().firmware_version << "\n"
                  << "  IP Address    : " << info.value().ip_address << "\n\n";
    }

    auto metrics = dev.get_system_metrics();
    if (metrics) {
        std::cout << "Real-time Telemetry:\n"
                  << "  CPU Temp      : " << std::fixed << std::setprecision(1) << metrics.value().cpu_temp_c << " °C\n"
                  << "  CPU Usage     : " << std::fixed << std::setprecision(1) << metrics.value().cpu_usage_percent << " %\n"
                  << "  RAM Used/Total: " << metrics.value().ram_used_mb << " / " << metrics.value().ram_total_mb << " MB\n"
                  << "  Disk (MB)     : " << metrics.value().disk_used_mb << " / " << metrics.value().disk_total_mb << " MB\n"
                  << "  Uptime        : " << metrics.value().uptime_seconds << " s\n";
    }

    return 0;
}
