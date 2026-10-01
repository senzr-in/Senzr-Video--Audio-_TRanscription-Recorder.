#pragma once

/**
 * Senzr OS C++ SDK — libsenzr
 *
 * Enterprise-grade embedded platform library for the Senzr device ecosystem.
 * Targets the Rockchip RK3588 SoC and any standard Linux-based edge device.
 *
 * Compatible boards (all using RK3588 / RK3588S):
 *   - Orange Pi 5 Max      (Shenzhen Xunlong)
 *   - Rock 5B              (Radxa)
 *   - NanoPC-T6            (FriendlyElec)
 *   - KHADAS Edge2         (KHADAS)
 *   - Firefly ITX-3588J    (Firefly)
 *   - Mixtile Blade 3      (Mixtile)
 *   - FET3588-C SoM        (Forlinx Embedded)  -- GPIO map: TODO (OK3588-C carrier pinout pending)
 *   - Any custom carrier with RK3588 module (LGA/SOM)
 *
 * API surface:
 *   senzr::Device          - Hardware identity, telemetry, system control
 *   senzr::Storage         - SQLite offline storage engine (WAL, crash-recovery, events)
 *   senzr::fota::Client    - FOTA update engine with A/B rollback
 *   senzr::gpio::Manager   - 40-pin GPIO, relay pulsing, Wiegand
 *   senzr::net::Manager    - Ethernet, Wi-Fi, DHCP/static configuration
 */

#include "senzr/device.hpp"
#include "senzr/storage.hpp"
#include "senzr/fota.hpp"
#include "senzr/gpio.hpp"
#include "senzr/network.hpp"

#define SENZR_SDK_VERSION_MAJOR 1
#define SENZR_SDK_VERSION_MINOR 0
#define SENZR_SDK_VERSION_PATCH 0
#define SENZR_SDK_VERSION_STRING "1.0.0"
