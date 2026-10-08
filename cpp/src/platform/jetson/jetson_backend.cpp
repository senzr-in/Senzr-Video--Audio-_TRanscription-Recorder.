#include "platform/jetson/jetson_backend.hpp"
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <algorithm>

namespace senzr::platform::jetson {

bool is_jetson_platform() {
    std::error_code ec;
    // 1. Check NVIDIA L4T release file
    if (std::filesystem::exists("/etc/nv_tegra_release", ec)) {
        return true;
    }
    // 2. Check Device Tree Model
    std::ifstream dt("/proc/device-tree/model");
    if (dt.is_open()) {
        std::string model;
        std::getline(dt, model);
        if (model.find("Jetson") != std::string::npos ||
            model.find("NVIDIA") != std::string::npos ||
            model.find("Tegra") != std::string::npos) {
            return true;
        }
    }
    // 3. Check compatible string
    std::ifstream dt_comp("/proc/device-tree/compatible");
    if (dt_comp.is_open()) {
        std::string comp;
        std::getline(dt_comp, comp);
        if (comp.find("nvidia,tegra210") != std::string::npos ||
            comp.find("nvidia,p3450") != std::string::npos ||
            comp.find("nvidia,jetson-nano") != std::string::npos) {
            return true;
        }
    }
    // 4. Check tegra thermal or devfreq
    if (std::filesystem::exists("/sys/devices/gpu.0", ec) ||
        std::filesystem::exists("/sys/devices/virtual/thermal/thermal_zone1/temp", ec)) {
        // Additional check for Tegra
        std::ifstream tname("/sys/devices/virtual/thermal/thermal_zone1/type");
        if (tname.is_open()) {
            std::string n;
            std::getline(tname, n);
            if (n.find("GPU-therm") != std::string::npos) return true;
        }
    }
    return false;
}

PlatformInfo get_jetson_info() {
    PlatformInfo info;
    info.platform = Platform::NvidiaJetsonNano;
    info.platform_name = "NVIDIA Jetson Nano (Tegra X1 / T210)";
    info.architecture = "aarch64";
    info.cpu_model = "Quad-Core ARM Cortex-A57 MPCore @ 1.43 GHz";
    info.cpu_cores = 4;

    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.ram_total_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    } else {
        info.ram_total_mb = 4096; // Standard 4GB Nano
    }

    info.os_info = "NVIDIA JetPack / Linux for Tegra (Ubuntu 18.04/20.04)";
    info.kernel_version = "4.9.253-tegra";

    // Accelerator
    info.accelerator.type = AcceleratorType::NvidiaGpuCuda;
    info.accelerator.name = "128-Core NVIDIA Maxwell GPU @ 921 MHz";
    info.accelerator.core_count = 128;
    info.accelerator.tops_or_tflops = 0.472; // 472 GFLOPS FP16 peak
    info.accelerator.is_available = true;

    info.capabilities = {
        Capability::Gpu,
        Capability::Cuda,
        Capability::TensorRT,
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

bool has_jetson_capability(Capability cap) {
    switch (cap) {
        case Capability::Gpu:
        case Capability::Cuda:
        case Capability::TensorRT:
        case Capability::Camera:
        case Capability::Gpio:
        case Capability::Ble:
        case Capability::Wifi:
        case Capability::Ethernet:
        case Capability::HardwareVideoEncoder:
        case Capability::HardwareVideoDecoder:
        case Capability::Watchdog:
            return true;
        case Capability::Npu:
        case Capability::Rknn:
            return false;
        default:
            return false;
    }
}

SystemMetrics get_jetson_metrics() {
    SystemMetrics m;

    // Thermal Zone 0 = CPU-therm, Thermal Zone 1 = GPU-therm on Jetson Nano
    std::ifstream tz0("/sys/devices/virtual/thermal/thermal_zone0/temp");
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

int map_jetson_pin(int pin_number) {
    // NVIDIA Jetson Nano J41 40-Pin Header -> Linux sysfs GPIO number mapping
    switch (pin_number) {
        case 7:  return 216; // Pin 7  -> AUD_MCLK / GPIO 216 (Relay)
        case 11: return 50;  // Pin 11 -> UART2_RTS / GPIO 50 (Buzzer)
        case 12: return 79;  // Pin 12 -> I2S0_SCLK / GPIO 79
        case 13: return 14;  // Pin 13 -> SPI1_SCK / GPIO 14 (Status LED)
        case 15: return 194; // Pin 15 -> LCD_TE / GPIO 194 (Safety Beam)
        case 16: return 232; // Pin 16 -> SPI1_CS1 / GPIO 232
        case 18: return 15;  // Pin 18 -> SPI1_CS0 / GPIO 15
        case 19: return 16;  // Pin 19 -> SPI0_MOSI / GPIO 16
        case 21: return 17;  // Pin 21 -> SPI0_MISO / GPIO 17
        case 22: return 13;  // Pin 22 -> SPI1_MISO / GPIO 13
        case 23: return 18;  // Pin 23 -> SPI0_SCK / GPIO 18
        case 24: return 19;  // Pin 24 -> SPI0_CS0 / GPIO 19
        case 26: return 20;  // Pin 26 -> SPI0_CS1 / GPIO 20
        case 29: return 149; // Pin 29 -> CAM_AF_EN / GPIO 149
        case 31: return 200; // Pin 31 -> GPIO_PZ0 / GPIO 200
        case 32: return 168; // Pin 32 -> LCD_BL_PWM / GPIO 168
        case 33: return 38;  // Pin 33 -> GPIO_PE6 / GPIO 38
        case 35: return 76;  // Pin 35 -> I2S0_FS / GPIO 76
        case 36: return 51;  // Pin 36 -> UART2_CTS / GPIO 51
        case 37: return 12;  // Pin 37 -> SPI1_MOSI / GPIO 12
        case 38: return 77;  // Pin 38 -> I2S0_DIN / GPIO 77
        case 40: return 78;  // Pin 40 -> I2S0_DOUT / GPIO 78
        default: return pin_number;
    }
}

} // namespace senzr::platform::jetson
