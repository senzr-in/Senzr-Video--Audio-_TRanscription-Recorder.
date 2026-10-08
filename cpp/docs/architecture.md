# Senzr OS SDK Architecture & Subsystem Guide

## High-Level Architecture

`libsenzr` provides an isolated, commercial-grade C++20 SDK for Rockchip RK3588/RK3588S Linux edge appliances.

```
+-------------------------------------------------------------+
|                     External Consumer App                   |
|               (Linked against Senzr::SDK)                   |
+-------------------------------------------------------------+
                              |
                              v  (include <senzr/senzr.hpp>)
+-------------------------------------------------------------+
|                 Senzr Public API Boundary                   |
|  - senzr::Device        - senzr::gpio::Manager              |
|  - senzr::Storage       - senzr::fota::Client               |
|  - senzr::net::Manager  - senzr::mqtt::Client               |
|  - senzr::ai::*         - senzr::media::Recorder            |
+-------------------------------------------------------------+
                              |
                              v  (Pimpl / Stable ABI)
+-------------------------------------------------------------+
|               Private Internal Implementations              |
|  - SQLite3 / SQLCipher  - libmosquitto TLS 1.3 / Port 8883  |
|  - RAUC D-Bus client    - RKNN / NPU Triple-Core            |
|  - V4L2 / Linux GStream - sysfs / Linux watchdog supervisor |
+-------------------------------------------------------------+
```

## Core Design Principles

1. **Strict Public/Private Boundary**:
   Public headers under `include/senzr/` contain zero third-party headers (`sqlite3.h`, `mosquitto.h`, `rknn_api.h`, `nlohmann/json.hpp`).
2. **Pimpl Encapsulation**:
   Heavy classes manage implementation details behind opaque pointers to ensure forward ABI compatibility.
3. **Structured Results & Error Codes**:
   All fallible operations return `senzr::Result<T>` instead of raw booleans or unhandled exceptions.
4. **Hardware Portability**:
   Platform-specific RK3588 paths (`/sys/class/thermal/`, `/dev/watchdog`, `/dev/rknpu`) are shielded inside SDK adapters.
