#include "senzr/senzr.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <cctype>

void print_help() {
    std::cout << "Senzr OS Command Line Management Tool (senzr-ctl)\n"
              << "Version: " << senzr::version_string() << "\n\n"
              << "Usage: senzr-ctl <module> <command> [options]\n\n"
              << "Modules:\n"
              << "  device info                    Display hardware MAC, serial, temperature & telemetry\n"
              << "  device reboot [delay_sec]      Reboot device\n"
              << "  fota status                    Print current version and updater status\n"
              << "  fota check                     Check S3 for available firmware releases\n"
              << "  fota update <ver> [--reboot]   Trigger OTA update to specified version\n"
              << "  fota rollback                  Roll back to the previous snapshot backup\n"
              << "  gpio list                      List mapped RK3588 GPIO pins & aliases\n"
              << "  gpio pulse <alias|pin> [ms]    Pulse pin (e.g. DOOR_RELAY 3000)\n"
              << "  gpio write <alias|pin> <1|0>   Set pin output state\n"
              << "  net status                     Display Ethernet and Wi-Fi interface status\n"
              << "  ble status                     Display BLE GATT service and connection status\n"
              << "  ble start / stop               Control BLE Mobile Access advertising daemon\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    std::string module = argv[1];
    std::string cmd = (argc >= 3) ? argv[2] : "";

    // ── Device Module ────────────────────────────────────────────────────────
    if (module == "device" || module == "info") {
        auto& dev = senzr::Device::get_instance();
        if (cmd == "reboot") {
            int delay = (argc >= 4) ? std::stoi(argv[3]) : 3;
            std::cout << "[senzr-ctl] Rebooting in " << delay << " seconds...\n";
            dev.reboot(delay);
            return 0;
        }

        auto info_res = dev.get_device_info();
        auto metrics_res = dev.get_system_metrics();

        std::string mac = dev.get_mac_address();
        std::string serial = dev.get_serial_number();
        std::string model = dev.get_board_model();
        std::string version = dev.get_firmware_version();
        std::string ip = dev.get_ip_address();

        double cpu_temp = metrics_res ? metrics_res.value().cpu_temp_c : 0.0;
        double cpu_usage = metrics_res ? metrics_res.value().cpu_usage_percent : 0.0;
        uint64_t ram_used = metrics_res ? metrics_res.value().ram_used_mb : 0;
        uint64_t ram_total = metrics_res ? metrics_res.value().ram_total_mb : 0;
        uint64_t disk_used = metrics_res ? metrics_res.value().disk_used_mb : 0;
        uint64_t disk_total = metrics_res ? metrics_res.value().disk_total_mb : 0;
        uint64_t uptime = metrics_res ? metrics_res.value().uptime_seconds : 0;

        std::cout << "===========================================\n"
                  << "           SENZR OS HARDWARE INFO          \n"
                  << "===========================================\n"
                  << "  MAC Address   : " << mac << "\n"
                  << "  Serial Number : " << serial << "\n"
                  << "  Board Model   : " << model << "\n"
                  << "  Firmware Ver  : " << version << "\n"
                  << "  IP Address    : " << ip << "\n"
                  << "  CPU Temp      : " << std::fixed << std::setprecision(1) << cpu_temp << " °C\n"
                  << "  CPU Usage     : " << std::fixed << std::setprecision(1) << cpu_usage << " %\n"
                  << "  RAM Usage     : " << ram_used << " / " << ram_total << " MB\n"
                  << "  Disk Usage    : " << disk_used << " / " << disk_total << " MB\n"
                  << "  Uptime        : " << uptime << " seconds\n"
                  << "===========================================\n";
        return 0;
    }

    // ── FOTA Module ──────────────────────────────────────────────────────────
    if (module == "fota" || module == "ota") {
        senzr::fota::Client client;

        if (cmd == "status" || cmd.empty()) {
            auto st = client.get_status();
            std::cout << "Current Version: " << st.current_version << "\n"
                      << "State          : " << st.state << "\n"
                      << "Updating       : " << (st.is_updating ? "true" : "false") << "\n";
            return 0;
        }

        if (cmd == "check") {
            std::cout << "[senzr-ctl] Checking S3 for latest releases...\n";
            auto meta_res = client.check_latest();
            if (meta_res && meta_res.value().has_value()) {
                const auto& meta = meta_res.value().value();
                std::cout << "Latest Version Available: " << meta.version << '\n'
                          << "Release Package         : " << meta.file_name << '\n'
                          << "SHA-256 Checksum        : " << meta.checksum_sha256 << '\n';
            } else {
                std::cout << "No release found or failed to fetch latest metadata\n";
            }
            return 0;
        }

        if (cmd == "update") {
            if (argc < 4) {
                std::cerr << "Usage: senzr-ctl fota update <version> [--reboot] [--url <url>] [--sig <signature>] [--allow-downgrade]\n";
                return 1;
            }
            std::string ver = argv[3];
            bool reboot = false;
            std::string direct_url = "";
            std::string signature = "";
            bool allow_downgrade = false;
            for (int i = 4; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--reboot") reboot = true;
                if ((arg == "--url" || arg == "-u") && i + 1 < argc) {
                    direct_url = argv[++i];
                }
                if ((arg == "--signature" || arg == "--sig") && i + 1 < argc) {
                    signature = argv[++i];
                }
                if (arg == "--allow-downgrade" || arg == "--force") {
                    allow_downgrade = true;
                }
            }

            std::cout << "[senzr-ctl] Starting update to version " << ver
                      << (reboot ? " (with post-update reboot)" : "") << "...\n";
            auto res = client.update(ver, reboot, direct_url, 0, signature, allow_downgrade);
            std::cout << (res ? "[senzr-ctl] Update successful!\n" : "[senzr-ctl] Update failed: " + res.error().message() + "\n");
            return res ? 0 : 1;
        }

        if (cmd == "rollback") {
            std::cout << "[senzr-ctl] Initiating manual rollback...\n";
            auto res = client.rollback();
            std::cout << (res ? "[senzr-ctl] Rollback completed.\n" : "[senzr-ctl] Rollback notice: " + res.error().message() + "\n");
            return res ? 0 : 1;
        }
    }

    // ── GPIO Module ──────────────────────────────────────────────────────────
    if (module == "gpio") {
        auto& mgr = senzr::gpio::get_manager();

        if (cmd == "list" || cmd.empty()) {
            std::cout << "Mapped RK3588 GPIO Pins:\n";
            for (const auto& pin : mgr.get_all_pins()) {
                std::cout << "  Pin " << pin.pin_number << " [" << pin.alias << "]: "
                          << (pin.mode == senzr::gpio::PinMode::Output ? "OUT" : "IN")
                          << " (State: " << (pin.state == senzr::gpio::PinState::High ? "HIGH" : "LOW") << ")\n";
            }
            return 0;
        }

        if (cmd == "pulse") {
            if (argc < 4) {
                std::cerr << "Usage: senzr-ctl gpio pulse <alias|pin> [duration_ms]\n";
                return 1;
            }
            std::string target = argv[3];
            int duration = (argc >= 5) ? std::stoi(argv[4]) : 3000;
            auto res = mgr.pulse_alias(target, duration);
            if (!res && std::isdigit(target[0])) {
                res = mgr.pulse(std::stoi(target), duration);
            }
            std::cout << (res ? "[senzr-ctl] Pulsed " + target + " for " + std::to_string(duration) + "ms\n"
                              : "[senzr-ctl] Failed to pulse target\n");
            return res ? 0 : 1;
        }

        if (cmd == "write") {
            if (argc < 5) {
                std::cerr << "Usage: senzr-ctl gpio write <alias|pin> <1|0>\n";
                return 1;
            }
            std::string target = argv[3];
            int val = std::stoi(argv[4]);
            auto state = (val != 0) ? senzr::gpio::PinState::High : senzr::gpio::PinState::Low;
            auto res = mgr.write_alias(target, state);
            if (!res && std::isdigit(target[0])) {
                res = mgr.digital_write(std::stoi(target), state);
            }
            std::cout << (res ? "[senzr-ctl] Pin state updated\n" : "[senzr-ctl] Failed to set pin state\n");
            return res ? 0 : 1;
        }
    }

    // ── Network Module ───────────────────────────────────────────────────────
    if (module == "net" || module == "network") {
        auto& net = senzr::net::get_manager();
        auto eth_res = net.get_ethernet_status();
        auto wifi_res = net.get_wifi_status();

        auto eth = eth_res ? eth_res.value() : senzr::net::EthernetStatus{};
        auto wifi = wifi_res ? wifi_res.value() : senzr::net::WifiStatus{};

        std::cout << "===========================================\n"
                  << "           SENZR OS NETWORK STATUS         \n"
                  << "===========================================\n"
                  << "  Ethernet Interface: " << eth.interface_name << "\n"
                  << "  Ethernet Status   : " << (eth.connected ? "Connected" : "Disconnected") << "\n"
                  << "  Ethernet IP       : " << eth.ip_address << "\n"
                  << "  Ethernet MAC      : " << eth.mac_address << "\n"
                  << "  Ethernet Gateway  : " << eth.gateway << "\n"
                  << "-------------------------------------------\n"
                  << "  Wi-Fi Status      : " << (wifi.connected ? "Connected" : "Disconnected") << "\n"
                  << "  Wi-Fi SSID        : " << (wifi.connected ? wifi.ssid : "-") << "\n"
                  << "  Wi-Fi IP          : " << (wifi.connected ? wifi.ip_address : "-") << "\n"
                  << "  Wi-Fi Signal      : " << wifi.signal_strength_dbm << " dBm\n"
                  << "===========================================\n";
        return 0;
    }

    // ── BLE Module ───────────────────────────────────────────────────────────
    if (module == "ble" || module == "bluetooth") {
        auto& ble = senzr::ble::get_manager();

        if (cmd == "status" || cmd.empty()) {
            auto st = ble.get_status();
            std::cout << "===========================================\n"
                      << "           SENZR OS BLE STATUS             \n"
                      << "===========================================\n"
                      << "  GATT Service UUID: 9F82A001-7B34-4C12-8921-5092A1B2C3D4\n"
                      << "  Challenge UUID   : 9F82A002-7B34-4C12-8921-5092A1B2C3D4\n"
                      << "  Response UUID    : 9F82A003-7B34-4C12-8921-5092A1B2C3D4\n"
                      << "  Advertising      : " << (st.is_advertising ? "ACTIVE (Broadcasting)" : "INACTIVE") << "\n"
                      << "  Connected Clients: " << (st.is_connected ? "CONNECTED" : "NO ACTIVE CLIENT") << "\n"
                      << "  Actuation Range  : Proximity Zone (>= -70 dBm)\n"
                      << "===========================================\n";
            return 0;
        }

        if (cmd == "start") {
            auto res = ble.start_advertising();
            std::cout << (res ? "[senzr-ctl] BLE Access GATT advertising started\n"
                              : "[senzr-ctl] Failed to start BLE: " + res.error().message() + "\n");
            return res ? 0 : 1;
        }

        if (cmd == "stop") {
            auto res = ble.stop_advertising();
            std::cout << (res ? "[senzr-ctl] BLE Access advertising stopped\n"
                              : "[senzr-ctl] Failed to stop BLE: " + res.error().message() + "\n");
            return res ? 0 : 1;
        }
    }

    print_help();
    return 1;
}
