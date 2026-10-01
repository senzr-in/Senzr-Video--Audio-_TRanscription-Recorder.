#include <senzr/senzr.hpp>
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "========================================\n"
              << "      Senzr OS SDK Application Demo     \n"
              << "      SDK Version: " << SENZR_SDK_VERSION_STRING << "\n"
              << "========================================\n\n";

    // 1. Device Hardware Information
    auto& dev = senzr::Device::get_instance();
    std::cout << "[Device]\n"
              << "  MAC Address   : " << dev.get_mac_address() << "\n"
              << "  Normalized MAC: " << dev.get_normalized_mac() << "\n"
              << "  Serial Number : " << dev.get_serial_number() << "\n"
              << "  Board Model   : " << dev.get_board_model() << "\n"
              << "  IP Address    : " << dev.get_ip_address() << "\n\n";

    // 2. Hardware Telemetry & Health
    auto metrics = dev.get_system_metrics();
    std::cout << "[Telemetry]\n"
              << "  CPU Temp      : " << std::fixed << std::setprecision(1) << metrics.cpu_temp_c << " °C\n"
              << "  CPU Usage     : " << std::fixed << std::setprecision(1) << metrics.cpu_usage_percent << " %\n"
              << "  RAM (Used/Tot): " << metrics.ram_used_mb << " / " << metrics.ram_total_mb << " MB\n"
              << "  Disk(Used/Tot): " << metrics.disk_used_mb << " / " << metrics.disk_total_mb << " MB\n"
              << "  Uptime        : " << metrics.uptime_seconds << " s\n\n";

    // 3. FOTA Engine
    senzr::fota::Client fota;
    std::cout << "[FOTA Engine]\n"
              << "  Current Vers. : " << fota.get_current_version() << "\n\n";

    // 4. GPIO Manager
    auto& gpio = senzr::gpio::get_manager();
    const auto& pins = gpio.get_all_pins();
    std::cout << "[GPIO Control]\n"
              << "  Total Pins    : " << pins.size() << "\n";
    for (const auto& pin : pins) {
        std::cout << "  - Pin " << pin.pin_number << " (" << pin.alias << "): "
                  << (pin.mode == senzr::gpio::PinMode::Output ? "OUT" : "IN")
                  << " [State: " << (pin.state == senzr::gpio::PinState::High ? "HIGH" : "LOW") << "]\n";
    }

    std::cout << "\n========================================\n"
              << "Senzr OS SDK initialized successfully!\n"
              << "========================================\n";
    return 0;
}
