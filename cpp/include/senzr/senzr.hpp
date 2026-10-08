#pragma once

/**
 * @file senzr.hpp
 * @brief Senzr Edge OS C++ SDK — libsenzr Master Umbrella Public Header
 *
 * Commercial-grade embedded platform SDK for the Senzr device ecosystem.
 * Targets the Rockchip RK3588/RK3588S SoC and Linux-based edge appliances.
 *
 * Compatible hardware:
 *   - Orange Pi 5 Max      (Shenzhen Xunlong)
 *   - Rock 5B              (Radxa)
 *   - NanoPC-T6            (FriendlyElec)
 *   - KHADAS Edge2         (KHADAS)
 *   - Standard aarch64 Linux Edge Platforms
 *
 * Public API Subsystems:
 *   - senzr::types               - Common SDK types and timestamps
 *   - senzr::error               - Standardized ErrorCode and Result<T> monad
 *   - senzr::version             - Semantic versioning constants and runtime query
 *   - senzr::log                 - Configurable logging system
 *   - senzr::Device              - Hardware identity, telemetry, system control
 *   - senzr::gpio::Manager       - 40-pin GPIO, non-blocking relay pulsing, safety beam
 *   - senzr::Storage             - Crash-resilient SQLite WAL offline storage
 *   - senzr::net::Manager        - Ethernet, Wi-Fi Station, and Wi-Fi AP Hotspot provisioning
 *   - senzr::watchdog::Supervisor- Hardware watchdog supervisor
 *   - senzr::ble::Manager        - Bluetooth Low Energy peripheral and provisioning
 *   - senzr::fota::Client        - Dual-slot A/B FOTA updates and recovery
 *   - senzr::mqtt::Client        - Bi-directional MQTTS cloud telemetry & downlinks
 *   - senzr::ai::PersonDetector  - YOLOv8 RK3588 NPU person detection
 *   - senzr::ai::AnprEngine      - Automatic Number Plate Recognition (Indian HSRP & Bharat series)
 *   - senzr::media::Recorder     - Event-driven video recording engine
 */

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/platform.hpp"
#include "senzr/error.hpp"
#include "senzr/version.hpp"
#include "senzr/logging.hpp"
#include "senzr/device.hpp"
#include "senzr/gpio.hpp"
#include "senzr/storage.hpp"
#include "senzr/network.hpp"
#include "senzr/watchdog.hpp"
#include "senzr/ble.hpp"
#include "senzr/fota.hpp"
#include "senzr/mqtt/client.hpp"
#include "senzr/ai/types.hpp"
#include "senzr/ai/inference.hpp"
#include "senzr/ai/person_detector.hpp"
#include "senzr/ai/anpr.hpp"
#include "senzr/media/camera.hpp"
#include "senzr/media/recorder.hpp"
