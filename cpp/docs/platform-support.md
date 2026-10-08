# Multi-Platform Support Matrix & Hardware Abstraction

## Overview

The Senzr OS C++ SDK (`libsenzr`) provides a unified, hardware-agnostic C++20 API across embedded Linux edge platforms:

1. **Rockchip RK3588 / RK3588S Platforms** (Orange Pi 5 Max, Radxa Rock 5B, FriendlyElec NanoPC-T6, Firefly ITX-3588J)
2. **NVIDIA Jetson Platforms** (Jetson Nano 4GB/2GB, Tegra X1 / T210, JetPack 4.6+)

## Architectural Layering

```
+-------------------------------------------------------------+
|                Portable Application Logic                   |
|                (Zero vendor #ifdef checks)                  |
+-------------------------------------------------------------+
                              |
                              v  (include <senzr/senzr.hpp>)
+-------------------------------------------------------------+
|                 Senzr Public API Layer                      |
|  - senzr::Device           - senzr::ai::InferenceEngine     |
|  - senzr::gpio::Manager    - senzr::media::CameraManager    |
|  - senzr::Storage          - senzr::fota::Client            |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|             Platform Abstraction Layer (HAL)                |
|  - Platform Detection: senzr::platform::detect_host_platform|
|  - Capability Discovery: device.has_capability(Capability)  |
|  - Dynamic Pin Mapping: map_logical_pin_to_sysfs()         |
+-------------------------------------------------------------+
               |                             |
               v                             v
+-----------------------------+ +-----------------------------+
|   Rockchip RK3588 Backend   | |  NVIDIA Jetson Nano Backend |
|  - RKNPU Triple-Core        | |  - 128 Maxwell CUDA Cores   |
|  - RK3588 40-Pin Sysfs/GPIO | |  - Jetson J41 40-Pin Header |
|  - Rockchip Thermal Zones   | |  - Tegra GPU/CPU Thermals   |
+-----------------------------+ +-----------------------------+
```

---

## Platform Support Matrix

| Subsystem / Feature | Rockchip RK3588 / RK3588S | NVIDIA Jetson Nano | Generic Linux / Host |
|---|---|---|---|
| **Platform Info & Identity** | ✅ Full Hardware Discovery | ✅ Full Hardware Discovery | ✅ Generic Linux Profile |
| **Telemetry & Thermals** | ✅ SoC Thermal Zone 0..3 | ✅ Tegra CPU / GPU Thermals | ✅ Standard sysfs |
| **GPIO Header Control** | ✅ RK3588 40-Pin Layout | ✅ Jetson J41 Pinout | ✅ Sysfs Fallback |
| **Safety Interlock Pulse** | ✅ Non-blocking `std::jthread` | ✅ Non-blocking `std::jthread` | ✅ Supported |
| **AI Inference** | ✅ RKNN Triple-Core (6 TOPS) | ✅ TensorRT / CUDA FP16 | ✅ Multi-thread CPU |
| **ANPR Normalization** | ✅ Stateless Thread-Safe | ✅ Stateless Thread-Safe | ✅ Stateless Thread-Safe |
| **Camera & Video Ingest** | ✅ V4L2 / MIPI CSI-2 | ✅ V4L2 / Tegra CSI-2 | ✅ V4L2 / USB / RTSP |
| **Offline SQLite Storage** | ✅ Crash-Resilient WAL | ✅ Crash-Resilient WAL | ✅ Crash-Resilient WAL |
| **MQTTS Cloud Sync** | ✅ TLS 1.3 Port 8883 | ✅ TLS 1.3 Port 8883 | ✅ TLS 1.3 Port 8883 |
| **Dual-Slot FOTA** | ✅ RAUC / A-B Slot Flashing | ✅ Dual Slot / Payload verify| ✅ Checksum Validation |
| **Hardware Watchdog** | ✅ `/dev/watchdog` Supervisor | ✅ `/dev/watchdog` (Tegra) | ✅ Linux Watchdog |

---

## CMake Platform Selection

Platform targeting can be specified at CMake configuration time or left on `auto` for dynamic probing:

```bash
# Auto-detect target platform dynamically
cmake -B build -DSENZR_PLATFORM=auto

# Explicitly target Rockchip RK3588
cmake -B build -DSENZR_PLATFORM=rockchip

# Explicitly target NVIDIA Jetson Nano
cmake -B build -DSENZR_PLATFORM=jetson
```
