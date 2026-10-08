#include "platform/rockchip/rk3588_backend.hpp"
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <algorithm>

namespace senzr::platform::rockchip {

bool is_rk3588_platform() {
    std::error_code ec;
    // Check device-tree model
    std::ifstream dt("/proc/device-tree/model");
    if (dt.is_open()) {
        std::string model;
        std::getline(dt, model);
        if (model.find("RK3588") != std::string::npos ||
            model.find("Orange Pi 5") != std::string::npos ||
            model.find("Rock 5") != std::string::npos ||
            model.find("NanoPC-T6") != std::string::npos) {
            return true;
        }
    }
    // Check compatible
    std::ifstream dt_comp("/proc/device-tree/compatible");
    if (dt_comp.is_open()) {
        std::string comp;
        std::getline(dt_comp, comp);
        if (comp.find("rk3588") != std::string::npos) {
            return true;
        }
    }
    // Check RKNPU driver
    if (std::filesystem::exists("/sys/kernel/debug/rknpu/load", ec) ||
        std::filesystem::exists("/dev/rknpu", ec) ||
        std::filesystem::exists("/sys/class/devfreq/fdab0000.npu", ec)) {
        return true;
    }
    return false;
}

PlatformInfo get_rk3588_info() {
    PlatformInfo info;
    info.platform = Platform::RockchipRK3588;
    info.platform_name = "Rockchip RK3588 / RK3588S (Octa-Core)";
    info.architecture = "aarch64";
    info.cpu_model = "4x Cortex-A76 @ 2.4GHz + 4x Cortex-A55 @ 1.8GHz";
    info.cpu_cores = 8;
    
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.ram_total_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    } else {
        info.ram_total_mb = 16384;
    }

    info.os_info = "Senzr OS (Linux aarch64)";
    info.kernel_version = "5.10.160-rockchip";

    // Accelerator
    info.accelerator.type = AcceleratorType::RockchipNpu;
    info.accelerator.name = "Rockchip Triple-Core NPU (NPU0, NPU1, NPU2)";
    info.accelerator.core_count = 3;
    info.accelerator.tops_or_tflops = 6.0; // 6.0 TOPS INT8 peak
    info.accelerator.is_available = true;

    info.capabilities = {
        Capability::Npu,
        Capability::Rknn,
        Capability::Gpu,
        Capability::Camera,
        Capability::Gpio,
        Capability::Ble,
        Capability::Wifi,
        Capability::Ethernet,
        Capability::HardwareVideoEncoder,
        Capability::HardwareVideoDecoder,
        Capability::Watchdog
    };

    return info;
}

bool has_rk3588_capability(Capability cap) {
    switch (cap) {
        case Capability::Npu:
        case Capability::Rknn:
        case Capability::Gpu:
        case Capability::Camera:
        case Capability::Gpio:
        case Capability::Ble:
        case Capability::Wifi:
        case Capability::Ethernet:
        case Capability::HardwareVideoEncoder:
        case Capability::HardwareVideoDecoder:
        case Capability::Watchdog:
            return true;
        case Capability::Cuda:
        case Capability::TensorRT:
            return false;
        default:
            return false;
    }
}

SystemMetrics get_rk3588_metrics() {
    SystemMetrics m;

    // Thermal Zone 0 = SoC CPU temp on RK3588
    std::ifstream tz0("/sys/class/thermal/thermal_zone0/temp");
    if (tz0.is_open()) {
        long raw = 0;
        if (tz0 >> raw) m.cpu_temp_c = static_cast<double>(raw) / 1000.0;
    }

    // Meminfo
    std::ifstream mem("/proc/meminfo");
    if (mem.is_open()) {
        std::string key, unit;
        long val = 0;
        long total_kb = 0, avail_kb = 0;
        while (mem >> key >> val >> unit) {
            if (key == "MemTotal:") total_kb = val;
            else if (key == "MemAvailable:") avail_kb = val;
        }
        m.ram_total_mb = total_kb / 1024;
        m.ram_used_mb = (total_kb - avail_kb) / 1024;
    }

    // Disk
    try {
        auto space = std::filesystem::space("/");
        m.disk_total_mb = space.capacity / (1024 * 1024);
        m.disk_used_mb = (space.capacity - space.available) / (1024 * 1024);
    } catch (...) {}

    // Uptime
    std::ifstream upt("/proc/uptime");
    if (upt.is_open()) {
        double sec = 0;
        if (upt >> sec) m.uptime_seconds = static_cast<uint64_t>(sec);
    }

    // CPU load
    double loads[3] = {0.0};
    if (getloadavg(loads, 3) > 0) {
        int cores = std::max(1, (int)sysconf(_SC_NPROCESSORS_ONLN));
        m.cpu_usage_percent = std::clamp((loads[0] / cores) * 100.0, 0.0, 100.0);
    }

    return m;
}

int map_rk3588_pin(int pin_number) {
    // Standard Orange Pi 5 Max / RK3588 40-Pin Header Mapping
    switch (pin_number) {
        case 7:  return 139; // GPIO4_B3 (DOOR_RELAY)
        case 11: return 140; // GPIO4_B4 (BUZZER)
        case 13: return 141; // GPIO4_B5 (STATUS_LED)
        case 15: return 144; // GPIO4_C0 (SAFETY_BEAM / SENSOR)
        case 16: return 145; // GPIO4_C1
        case 18: return 146; // GPIO4_C2
        case 22: return 147; // GPIO4_C3
        case 29: return 148; // GPIO4_C4
        case 31: return 149; // GPIO4_C5
        case 32: return 150; // GPIO4_C6
        case 33: return 151; // GPIO4_C7
        case 35: return 131; // GPIO4_A3
        case 36: return 132; // GPIO4_A4
        case 37: return 133; // GPIO4_A5
        case 38: return 134; // GPIO4_A6
        case 40: return 135; // GPIO4_A7
        default: return pin_number;
    }
}

} // namespace senzr::platform::rockchip
