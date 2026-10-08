#include "platform/common/platform_detect.hpp"
#include "platform/rockchip/rk3588_backend.hpp"
#include "platform/jetson/jetson_backend.hpp"
#include <cstdlib>
#include <mutex>
#include <fstream>
#include <thread>

#ifndef _WIN32
#include <sys/utsname.h>
#endif

namespace senzr::platform {

static Platform s_cached_platform = Platform::Unknown;
static std::mutex s_platform_mutex;

Platform detect_host_platform() {
    std::lock_guard<std::mutex> lock(s_platform_mutex);
    if (s_cached_platform != Platform::Unknown) {
        return s_cached_platform;
    }

    // 1. Check runtime environment override (e.g. SENZR_TARGET_PLATFORM=rockchip|jetson)
    const char* env_p = std::getenv("SENZR_TARGET_PLATFORM");
    if (env_p) {
        std::string p_str = env_p;
        if (p_str == "rockchip" || p_str == "rk3588") {
            s_cached_platform = Platform::RockchipRK3588;
            return s_cached_platform;
        } else if (p_str == "jetson" || p_str == "jetson_nano" || p_str == "nvidia") {
            s_cached_platform = Platform::NvidiaJetsonNano;
            return s_cached_platform;
        }
    }

#if defined(SENZR_PLATFORM_ROCKCHIP)
    s_cached_platform = Platform::RockchipRK3588;
    return s_cached_platform;
#elif defined(SENZR_PLATFORM_JETSON)
    s_cached_platform = Platform::NvidiaJetsonNano;
    return s_cached_platform;
#endif

    // 2. Dynamic Hardware Probing
    if (rockchip::is_rk3588_platform()) {
        s_cached_platform = Platform::RockchipRK3588;
    } else if (jetson::is_jetson_platform()) {
        s_cached_platform = Platform::NvidiaJetsonNano;
    } else {
        s_cached_platform = Platform::GenericLinux;
    }

    return s_cached_platform;
}

PlatformInfo query_platform_info() {
    Platform p = detect_host_platform();
    if (p == Platform::RockchipRK3588) {
        return rockchip::get_rk3588_info();
    } else if (p == Platform::NvidiaJetsonNano) {
        return jetson::get_jetson_info();
    }

    // Generic Linux Probing
    PlatformInfo info;
    info.platform = Platform::GenericLinux;
    info.platform_name = "Generic Linux Edge Host";

#ifndef _WIN32
    struct utsname uts{};
    if (uname(&uts) == 0) {
        info.architecture = uts.machine;
        info.kernel_version = uts.release;
        info.os_info = std::string(uts.sysname) + " " + uts.release;
    } else {
#if defined(__aarch64__)
        info.architecture = "aarch64";
#elif defined(__x86_64__)
        info.architecture = "x86_64";
#else
        info.architecture = "unknown";
#endif
        info.kernel_version = "Linux";
        info.os_info = "Linux";
    }
#else
    info.architecture = "x86_64";
    info.kernel_version = "Windows/NT";
    info.os_info = "Windows";
#endif

    info.cpu_model = "Host CPU";
    info.cpu_cores = static_cast<int>(std::thread::hardware_concurrency());
    if (info.cpu_cores <= 0) info.cpu_cores = 4;
    info.ram_total_mb = 8192;
    info.accelerator.type = AcceleratorType::CpuFallback;
    info.accelerator.name = "Host CPU Software Fallback";
    info.accelerator.is_available = true;
    info.capabilities = {
        Capability::Camera,
        Capability::Gpio,
        Capability::Ethernet,
        Capability::Watchdog
    };
    return info;
}

bool query_capability(Capability cap) {
    Platform p = detect_host_platform();
    if (p == Platform::RockchipRK3588) {
        return rockchip::has_rk3588_capability(cap);
    } else if (p == Platform::NvidiaJetsonNano) {
        return jetson::has_jetson_capability(cap);
    }
    // Generic
    switch (cap) {
        case Capability::Camera:
        case Capability::Gpio:
        case Capability::Ethernet:
        case Capability::Watchdog:
            return true;
        default:
            return false;
    }
}

SystemMetrics query_system_metrics() {
    Platform p = detect_host_platform();
    if (p == Platform::RockchipRK3588) {
        return rockchip::get_rk3588_metrics();
    } else if (p == Platform::NvidiaJetsonNano) {
        return jetson::get_jetson_metrics();
    }
    return rockchip::get_rk3588_metrics();
}

int map_logical_pin_to_sysfs(int pin_number) {
    Platform p = detect_host_platform();
    if (p == Platform::NvidiaJetsonNano) {
        return jetson::map_jetson_pin(pin_number);
    }
    return rockchip::map_rk3588_pin(pin_number);
}

} // namespace senzr::platform
