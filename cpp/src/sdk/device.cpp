#include "senzr/device.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <algorithm>
#include <chrono>
#include <thread>
#include <filesystem>
#include <sys/statvfs.h>
#include <unistd.h>

namespace senzr {

Device& Device::get_instance() {
    static Device instance;
    return instance;
}

std::string Device::exec_cmd(const std::string& cmd) {
    std::array<char, 256> buffer{};
    std::string result;
#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
    if (!pipe) return "";
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string Device::get_mac_address(const std::string& preferred_iface) {
    if (!cached_mac_.empty()) return cached_mac_;

    std::vector<std::string> ifaces;
    if (!preferred_iface.empty()) ifaces.push_back(preferred_iface);
    ifaces.insert(ifaces.end(), {"eth0", "end0", "eth1", "wlan0"});

    for (const auto& iface : ifaces) {
        std::filesystem::path p = "/sys/class/net/" + iface + "/address";
        if (std::filesystem::exists(p)) {
            std::ifstream f(p);
            std::string mac;
            if (std::getline(f, mac) && !mac.empty()) {
                auto start = mac.find_first_not_of(" \t\r\n");
                auto end   = mac.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    mac = mac.substr(start, end - start + 1);
                }
                if (!mac.empty() && mac != "00:00:00:00:00:00") {
                    cached_mac_ = mac;
                    return mac;
                }
            }
        }
    }
    return "00:00:00:00:00:00";
}

std::string Device::get_normalized_mac() {
    std::string raw = get_mac_address();
    std::string clean;
    for (char c : raw) {
        if (c != ':' && c != '-' && c != ' ') {
            clean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return clean;
}

std::string Device::get_serial_number() {
    if (!cached_serial_.empty()) return cached_serial_;

    // 1. Check device-tree
    if (std::filesystem::exists("/proc/device-tree/serial-number")) {
        std::ifstream f("/proc/device-tree/serial-number");
        std::string s;
        if (std::getline(f, s) && !s.empty()) {
            cached_serial_ = s;
            return s;
        }
    }

    // 2. Check /etc/machine-id
    if (std::filesystem::exists("/etc/machine-id")) {
        std::ifstream f("/etc/machine-id");
        std::string s;
        if (f >> s && !s.empty()) {
            cached_serial_ = s;
            return s;
        }
    }

    cached_serial_ = get_normalized_mac();
    return cached_serial_;
}

std::string Device::get_board_model() {
    // Try multiple device-tree model paths — works on any RK3588 board
    // (Orange Pi 5 Max, Rock 5B, NanoPC-T6, KHADAS Edge2, Firefly ITX-3588J, etc.)
    for (const auto& path : {
            "/proc/device-tree/model",
            "/sys/firmware/devicetree/base/model",
            "/proc/device-tree/compatible"
        }) {
        std::filesystem::path p = path;
        if (std::filesystem::exists(p)) {
            std::ifstream f(p);
            std::string model;
            if (std::getline(f, model) && !model.empty()) {
                // Trim null terminators that device-tree files can embed
                model.erase(std::find(model.begin(), model.end(), '\0'), model.end());
                if (!model.empty()) return model;
            }
        }
    }
    // Generic fallback: RK3588 SoC platform (no specific board assumed)
    return "RK3588 Platform";
}

std::string Device::get_firmware_version() {
    std::filesystem::path p = "/opt/edge-gateway/current/.version";
    if (std::filesystem::exists(p)) {
        std::ifstream f(p);
        std::string ver;
        if (f >> ver && !ver.empty()) return ver;
    }
    return "1.0.0";
}

std::string Device::get_ip_address() {
    std::string out = exec_cmd("hostname -I 2>/dev/null | awk '{print $1}'");
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    return out.empty() ? "127.0.0.1" : out;
}

SystemMetrics Device::get_system_metrics() {
    SystemMetrics m;

    // CPU Temp
    std::filesystem::path temp_path = "/sys/class/thermal/thermal_zone0/temp";
    if (std::filesystem::exists(temp_path)) {
        std::ifstream f(temp_path);
        long raw_temp = 0;
        if (f >> raw_temp) {
            m.cpu_temp_c = raw_temp / 1000.0;
        }
    }

    // Uptime
    std::filesystem::path up_path = "/proc/uptime";
    if (std::filesystem::exists(up_path)) {
        std::ifstream f(up_path);
        double up = 0;
        if (f >> up) {
            m.uptime_seconds = static_cast<uint64_t>(up);
        }
    }

    // RAM
    std::filesystem::path mem_path = "/proc/meminfo";
    if (std::filesystem::exists(mem_path)) {
        std::ifstream f(mem_path);
        std::string line;
        uint64_t total_kb = 0, avail_kb = 0;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::sscanf(line.c_str(), "MemTotal: %lu kB", &total_kb);
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::sscanf(line.c_str(), "MemAvailable: %lu kB", &avail_kb);
            }
        }
        m.ram_total_mb = total_kb / 1024;
        m.ram_used_mb = (total_kb >= avail_kb) ? (total_kb - avail_kb) / 1024 : 0;
    }

    // Storage
    struct statvfs vfs{};
    if (statvfs("/", &vfs) == 0) {
        m.disk_total_mb = (vfs.f_blocks * vfs.f_frsize) / (1024 * 1024);
        m.disk_used_mb  = ((vfs.f_blocks - vfs.f_bfree) * vfs.f_frsize) / (1024 * 1024);
    }

    // CPU Usage % from /proc/stat
    static unsigned long long prev_idle = 0, prev_total = 0;
    std::ifstream stat_file("/proc/stat");
    if (stat_file.is_open()) {
        std::string cpu_label;
        unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (stat_file >> cpu_label >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) {
            unsigned long long idle_time = idle + iowait;
            unsigned long long total_time = user + nice + sys + idle + iowait + irq + softirq + steal;
            if (prev_total > 0 && total_time > prev_total) {
                unsigned long long diff_idle = idle_time - prev_idle;
                unsigned long long diff_total = total_time - prev_total;
                m.cpu_usage_percent = 100.0 * (1.0 - static_cast<double>(diff_idle) / static_cast<double>(diff_total));
                if (m.cpu_usage_percent < 0.0) m.cpu_usage_percent = 0.0;
                if (m.cpu_usage_percent > 100.0) m.cpu_usage_percent = 100.0;
            }
            prev_idle = idle_time;
            prev_total = total_time;
        }
    }

    return m;
}

nlohmann::json Device::get_telemetry_json() {
    auto m = get_system_metrics();
    return {
        {"device_id", get_mac_address()},
        {"mac", get_mac_address()},
        {"serial", get_serial_number()},
        {"model", get_board_model()},
        {"version", get_firmware_version()},
        {"ip_address", get_ip_address()},
        {"cpu_temp_c", m.cpu_temp_c},
        {"cpu_usage_percent", m.cpu_usage_percent},
        {"ram_total_mb", m.ram_total_mb},
        {"ram_used_mb", m.ram_used_mb},
        {"disk_total_mb", m.disk_total_mb},
        {"disk_used_mb", m.disk_used_mb},
        {"uptime_sec", m.uptime_seconds},
        {"status", "online"}
    };
}

bool Device::reboot(int delay_seconds) {
    if (delay_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    return (std::system("systemctl reboot || reboot") == 0);
}

bool Device::shutdown(int delay_seconds) {
    if (delay_seconds > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
    }
    return (std::system("systemctl poweroff || poweroff") == 0);
}

} // namespace senzr
