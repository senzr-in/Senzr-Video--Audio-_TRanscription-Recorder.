#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace senzr {

/**
 * @brief Supported target edge computing hardware platforms.
 */
enum class Platform {
    Unknown = 0,
    RockchipRK3588,
    NvidiaJetsonNano,
    GenericLinux
};

/**
 * @brief Discrete hardware and runtime accelerator capabilities.
 */
enum class Capability {
    Gpu,
    Npu,
    Cuda,
    TensorRT,
    Rknn,
    Camera,
    Gpio,
    Ble,
    Wifi,
    Ethernet,
    HardwareVideoEncoder,
    HardwareVideoDecoder,
    Watchdog
};

/**
 * @brief Hardware AI/ML acceleration unit architecture.
 */
enum class AcceleratorType {
    None = 0,
    RockchipNpu,     // Rockchip RK3588 3-Core NPU (6 TOPS peak)
    NvidiaGpuCuda,   // NVIDIA Maxwell 128 CUDA Core GPU (Jetson Nano)
    CpuFallback
};

/**
 * @brief Detailed accelerator hardware specification.
 */
struct AcceleratorInfo {
    AcceleratorType type{AcceleratorType::None};
    std::string name;
    int core_count{0};
    double tops_or_tflops{0.0};
    bool is_available{false};
};

/**
 * @brief Comprehensive platform environment and capabilities profile.
 */
struct PlatformInfo {
    Platform platform{Platform::Unknown};
    std::string platform_name;
    std::string architecture;
    std::string cpu_model;
    int cpu_cores{0};
    uint64_t ram_total_mb{0};
    std::string os_info;
    std::string kernel_version;
    AcceleratorInfo accelerator;
    std::vector<Capability> capabilities;
};

/**
 * @brief Convert a Platform enum value to human-readable string.
 */
inline const char* platform_to_string(Platform p) {
    switch (p) {
        case Platform::RockchipRK3588:  return "Rockchip RK3588 / RK3588S";
        case Platform::NvidiaJetsonNano: return "NVIDIA Jetson Nano";
        case Platform::GenericLinux:     return "Generic Linux (Host/ARM64)";
        default:                         return "Unknown Platform";
    }
}

/**
 * @brief Convert a Capability enum value to human-readable string.
 */
inline const char* capability_to_string(Capability c) {
    switch (c) {
        case Capability::Gpu:                  return "GPU";
        case Capability::Npu:                  return "NPU";
        case Capability::Cuda:                 return "CUDA";
        case Capability::TensorRT:             return "TensorRT";
        case Capability::Rknn:                 return "RKNN";
        case Capability::Camera:               return "Camera";
        case Capability::Gpio:                 return "GPIO";
        case Capability::Ble:                  return "BLE";
        case Capability::Wifi:                 return "WiFi";
        case Capability::Ethernet:             return "Ethernet";
        case Capability::HardwareVideoEncoder: return "Hardware Video Encoder";
        case Capability::HardwareVideoDecoder: return "Hardware Video Decoder";
        case Capability::Watchdog:             return "Hardware Watchdog";
        default:                               return "Unknown Capability";
    }
}

} // namespace senzr
