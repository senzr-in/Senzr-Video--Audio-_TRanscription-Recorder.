#include "senzr/device.hpp"
#include "platform/common/platform_detect.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <algorithm>
#include <chrono>
#include <thread>
#include <filesystem>
#include <mutex>
#include <sys/statvfs.h>
#include <unistd.h>

namespace senzr {

struct Device::Impl {
    std::mutex mutex;
    std::string cached_mac;
    std::string cached_serial;

    static std::string exec_cmd(const std::string& cmd) {
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
};

Device::Device() : pimpl_(std::make_unique<Impl>()) {}
Device::~Device() = default;

Device& Device::get_instance() {
    static Device instance;
    return instance;
}

std::string Device::get_mac_address(const std::string& preferred_iface) {
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->cached_mac.empty()) return pimpl_->cached_mac;

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
                    pimpl_->cached_mac = mac;
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
    std::lock_guard<std::mutex> lock(pimpl_->mutex);
    if (!pimpl_->cached_serial.empty()) return pimpl_->cached_serial;

    // 1. Check device-tree
    if (std::filesystem::exists("/proc/device-tree/serial-number")) {
        std::ifstream f("/proc/device-tree/serial-number");
        std::string s;
        if (std::getline(f, s) && !s.empty()) {
            pimpl_->cached_serial = s;
            return s;
        }
    }

    // 2. Check /etc/machine-id
    if (std::filesystem::exists("/etc/machine-id")) {
        std::ifstream f("/etc/machine-id");
        std::string s;
        if (f >> s && !s.empty()) {
            pimpl_->cached_serial = s;
            return s;
        }
    }

    // Fallback to normalized MAC
    std::string norm = "";
    for (char c : (pimpl_->cached_mac.empty() ? "00:00:00:00:00:00" : pimpl_->cached_mac)) {
        if (c != ':' && c != '-' && c != ' ') {
            norm += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    pimpl_->cached_serial = "DEVICE_UUID_" + (norm.empty() ? "DEFAULT" : norm);
    return pimpl_->cached_serial;
}

std::string Device::get_board_model() {
    if (std::filesystem::exists("/proc/device-tree/model")) {
        std::ifstream f("/proc/device-tree/model");
        std::string s;
        if (std::getline(f, s) && !s.empty()) return s;
    }
    return "Orange Pi 5 Max (RK3588)";
}

std::string Device::get_firmware_version() {
    for (const auto& path : {
        "/opt/edge-gateway/version.txt",
        "/opt/edge-gateway/.version",
        "/opt/edge-gateway/current/version.txt",
        "/opt/edge-gateway/current/.version",
        "version.txt",
        ".version",
        "/etc/senzr-version"
    }) {
        if (std::filesystem::exists(path)) {
            std::ifstream f(path);
            std::string v;
            if (f >> v && !v.empty()) return v;
        }
    }
    return "1.1.0";
}

std::string Device::get_ip_address() {
    for (const auto& iface : {"eth0", "end0", "eth1", "wlan0"}) {
        std::string cmd = "ip -4 addr show " + std::string(iface) + " 2>/dev/null | grep -oP '(?<=inet\\s)\\d+(\\.\\d+){3}' | head -n 1";
        std::string ip = Impl::exec_cmd(cmd);
        auto start = ip.find_first_not_of(" \t\r\n");
        auto end   = ip.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            ip = ip.substr(start, end - start + 1);
            if (!ip.empty()) return ip;
        }
    }
    return "127.0.0.1";
}

Result<DeviceInfo> Device::get_device_info() {
    DeviceInfo info;
    info.mac_address = get_mac_address();
    info.normalized_mac = get_normalized_mac();
    info.serial_number = get_serial_number();
    info.board_model = get_board_model();
    info.firmware_version = get_firmware_version();
    info.ip_address = get_ip_address();
    return info;
}

Result<SystemMetrics> Device::get_system_metrics() {
    SystemMetrics m;

    // 1. CPU Temperature (thermal_zone0)
    for (const auto& tz : {"/sys/class/thermal/thermal_zone0/temp", "/sys/class/hwmon/hwmon0/temp1_input"}) {
        if (std::filesystem::exists(tz)) {
            std::ifstream f(tz);
            long raw_temp = 0;
            if (f >> raw_temp && raw_temp > 0) {
                m.cpu_temp_c = static_cast<double>(raw_temp) / 1000.0;
                break;
            }
        }
    }

    // 2. RAM Info (/proc/meminfo)
    if (std::filesystem::exists("/proc/meminfo")) {
        std::ifstream f("/proc/meminfo");
        std::string line;
        uint64_t mem_total_kb = 0;
        uint64_t mem_avail_kb = 0;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::sscanf(line.c_str(), "MemTotal: %lu kB", &mem_total_kb);
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::sscanf(line.c_str(), "MemAvailable: %lu kB", &mem_avail_kb);
            }
        }
        m.ram_total_mb = mem_total_kb / 1024;
        if (mem_total_kb >= mem_avail_kb) {
            m.ram_used_mb = (mem_total_kb - mem_avail_kb) / 1024;
        }
    }

    // 3. Disk Info (/)
    struct statvfs vfs{};
    if (statvfs("/", &vfs) == 0) {
        uint64_t total_b = static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize;
        uint64_t free_b  = static_cast<uint64_t>(vfs.f_bfree) * vfs.f_frsize;
        m.disk_total_mb = total_b / (1024 * 1024);
        m.disk_used_mb  = (total_b - free_b) / (1024 * 1024);
    }

    // 4. Uptime (/proc/uptime)
    if (std::filesystem::exists("/proc/uptime")) {
        std::ifstream f("/proc/uptime");
        double up = 0.0;
        if (f >> up) {
            m.uptime_seconds = static_cast<uint64_t>(up);
        }
    }

    // 5. CPU Usage (/proc/stat snapshot)
    if (std::filesystem::exists("/proc/stat")) {
        std::ifstream f("/proc/stat");
        std::string cpu;
        uint64_t user = 0, nice = 0, system = 0, idle = 0;
        if (f >> cpu >> user >> nice >> system >> idle) {
            uint64_t total = user + nice + system + idle;
            if (total > 0) {
                m.cpu_usage_percent = (1.0 - (static_cast<double>(idle) / total)) * 100.0;
            }
        }
    }

    return m;
}

Platform Device::get_platform() {
    return platform::detect_host_platform();
}

Result<PlatformInfo> Device::get_platform_info() {
    return Result<PlatformInfo>::success(platform::query_platform_info());
}

bool Device::has_capability(Capability cap) {
    return platform::query_capability(cap);
}

Result<void> Device::reboot(int delay_seconds) {
    std::thread([delay_seconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
        Impl::exec_cmd("reboot || sudo -n reboot 2>/dev/null");
    }).detach();
    return Result<void>::success();
}

Result<void> Device::shutdown(int delay_seconds) {
    std::thread([delay_seconds]() {
        std::this_thread::sleep_for(std::chrono::seconds(delay_seconds));
        Impl::exec_cmd("poweroff || sudo -n poweroff 2>/dev/null");
    }).detach();
    return Result<void>::success();
}

Result<void> Device::ensure_service_installed(const std::string& service_name) {
    std::filesystem::path service_file = "/etc/systemd/system/" + service_name + ".service";
    if (std::filesystem::exists(service_file)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NotSupported, "Service unit not present in /etc/systemd/system");
}

Result<void> Device::control_service(const std::string& action, const std::string& service_name) {
    if (action != "start" && action != "stop" && action != "restart" && action != "reload") {
        return Result<void>(ErrorCode::InvalidArgument, "Invalid service action: " + action);
    }
    std::string cmd = "systemctl " + action + " " + service_name + " 2>/dev/null || sudo -n systemctl " + action + " " + service_name + " 2>/dev/null";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        return Result<void>(ErrorCode::InternalError, "Failed to execute service action " + action);
    }
    return Result<void>::success();
}

} // namespace senzr
