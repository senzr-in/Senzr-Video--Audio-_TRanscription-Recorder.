#include "senzr/senzr.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

void print_help() {
    std::cout << "Senzr OS Command Line Management Tool (senzr-ctl)\n"
              << "Version: " << SENZR_SDK_VERSION_STRING << "\n\n"
              << "Usage: senzr-ctl <module> <command> [options]\n\n"
              << "Modules:\n"
              << "  device info                    Display hardware MAC, serial, temperature & telemetry\n"
              << "  device reboot [delay_sec]      Reboot device\n"
              << "  fota status                    Print current version and updater configuration\n"
              << "  fota check                     Check S3 for available firmware releases\n"
              << "  fota update <ver> [--reboot]   Trigger OTA update to specified version\n"
              << "  fota rollback                  Roll back to the previous snapshot backup\n"
              << "  gpio list                      List mapped RK3588 GPIO pins & aliases\n"
              << "  gpio pulse <alias|pin> [ms]    Pulse pin (e.g. DOOR_RELAY 3000)\n"
              << "  gpio write <alias|pin> <1|0>   Set pin output state\n"
              << "  net status                     Display Ethernet and Wi-Fi interface status\n";
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

        auto j = dev.get_telemetry_json();
        std::cout << "===========================================\n"
                  << "           SENZR OS HARDWARE INFO          \n"
                  << "===========================================\n"
                  << "  MAC Address   : " << j["mac"].get<std::string>() << "\n"
                  << "  Serial Number : " << j["serial"].get<std::string>() << "\n"
                  << "  Board Model   : " << j["model"].get<std::string>() << "\n"
                  << "  Firmware Ver  : " << j["version"].get<std::string>() << "\n"
                  << "  IP Address    : " << j["ip_address"].get<std::string>() << "\n"
                  << "  CPU Temp      : " << std::fixed << std::setprecision(1) << j["cpu_temp_c"].get<double>() << " °C\n"
                  << "  CPU Usage     : " << std::fixed << std::setprecision(1) << j["cpu_usage_percent"].get<double>() << " %\n"
                  << "  RAM Usage     : " << j["ram_used_mb"].get<uint64_t>() << " / " << j["ram_total_mb"].get<uint64_t>() << " MB\n"
                  << "  Disk Usage    : " << j["disk_used_mb"].get<uint64_t>() << " / " << j["disk_total_mb"].get<uint64_t>() << " MB\n"
                  << "  Uptime        : " << j["uptime_sec"].get<uint64_t>() << " seconds\n"
                  << "===========================================\n";
        return 0;
    }

    // ── FOTA Module ──────────────────────────────────────────────────────────
    if (module == "fota" || module == "ota") {
        senzr::fota::Client client;

        if (cmd == "status" || cmd.empty()) {
            std::cout << client.get_status_json().dump(2) << '\n';
            return 0;
        }

        if (cmd == "check") {
            std::cout << "[senzr-ctl] Checking S3 for latest releases...\n";
            auto meta = client.check_latest();
            if (meta) {
                std::cout << "Latest Version Available: " << meta->version << '\n'
                          << "Release Package         : " << meta->file << '\n'
                          << "SHA-256 Checksum        : " << meta->checksum << '\n';
            } else {
                std::cout << "No release found or failed to fetch latest.json\n";
            }
            return 0;
        }

        if (cmd == "update") {
            if (argc < 4) {
                std::cerr << "Usage: senzr-ctl fota update <version> [--reboot] [--url <url>]\n";
                return 1;
            }
            std::string ver = argv[3];
            bool reboot = false;
            std::string direct_url = "";
            for (int i = 4; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--reboot") reboot = true;
                if (arg == "--url" && i + 1 < argc) {
                    direct_url = argv[++i];
                }
            }

            std::cout << "[senzr-ctl] Starting update to version " << ver
                      << (reboot ? " (with post-update reboot)" : "") << "...\n";
            bool ok = client.update(ver, reboot, direct_url);
            std::cout << (ok ? "[senzr-ctl] Update successful!\n" : "[senzr-ctl] Update failed.\n");
            return ok ? 0 : 1;
        }

        if (cmd == "rollback") {
            std::cout << "[senzr-ctl] Initiating manual rollback...\n";
            bool ok = client.rollback();
            std::cout << (ok ? "[senzr-ctl] Rollback completed.\n" : "[senzr-ctl] Rollback failed.\n");
            return ok ? 0 : 1;
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
            bool ok = mgr.pulse_alias(target, duration);
            if (!ok && std::isdigit(target[0])) {
                ok = mgr.pulse_pin(std::stoi(target), duration);
            }
            std::cout << (ok ? "[senzr-ctl] Pulsed " + target + " for " + std::to_string(duration) + "ms\n"
                             : "[senzr-ctl] Failed to pulse target\n");
            return ok ? 0 : 1;
        }

        if (cmd == "write") {
            if (argc < 5) {
                std::cerr << "Usage: senzr-ctl gpio write <alias|pin> <1|0>\n";
                return 1;
            }
            std::string target = argv[3];
            int val = std::stoi(argv[4]);
            auto state = (val != 0) ? senzr::gpio::PinState::High : senzr::gpio::PinState::Low;
            bool ok = mgr.set_alias_state(target, state);
            if (!ok && std::isdigit(target[0])) {
                ok = mgr.set_pin_state(std::stoi(target), state);
            }
            std::cout << (ok ? "[senzr-ctl] Pin state updated\n" : "[senzr-ctl] Failed to set pin state\n");
            return ok ? 0 : 1;
        }
    }

    // ── Network Module ───────────────────────────────────────────────────────
    if (module == "net" || module == "network") {
        auto& net = senzr::net::get_manager();
        auto eth = net.get_ethernet_status();
        auto wifi = net.get_wifi_status();

        std::cout << "===========================================\n"
                  << "           SENZR OS NETWORK STATUS         \n"
                  << "===========================================\n"
                  << "  Ethernet Interface: " << eth.interface_name << "\n"
                  << "  Ethernet Status   : " << (eth.connected ? "Connected" : "Disconnected") << "\n"
                  << "  Ethernet IP       : " << eth.ip_address << "\n"
                  << "  Ethernet MAC      : " << eth.mac_address << "\n"
                  << "  Ethernet Gateway  : " << eth.gateway << "\n"
                  << "  Ethernet Speed    : " << eth.speed_mbps << " Mbps (" << eth.duplex << ")\n"
                  << "-------------------------------------------\n"
                  << "  Wi-Fi Status      : " << (wifi.connected ? "Connected" : "Disconnected") << "\n"
                  << "  Wi-Fi SSID        : " << (wifi.connected ? wifi.ssid : "-") << "\n"
                  << "  Wi-Fi IP          : " << (wifi.connected ? wifi.ip_address : "-") << "\n"
                  << "  Wi-Fi Signal      : " << wifi.signal_strength << " %\n"
                  << "===========================================\n";
        return 0;
    }

    print_help();
    return 1;
}
