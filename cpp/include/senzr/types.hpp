#pragma once

#include "senzr/export.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>
#include <string_view>

namespace senzr {
inline namespace v1 {

/**
 * @brief Standard high-resolution timestamp representation.
 */
using Timestamp = std::chrono::time_point<std::chrono::system_clock>;

/**
 * @brief Common device and operational state representation.
 */
enum class OperationalState {
    Unknown = 0,
    Initializing,
    Idle,
    Active,
    Warning,
    Error,
    Updating,
    ShuttingDown
};

/**
 * @brief Device unique identification package.
 */
struct SENZR_API DeviceId {
    std::string mac_address;       // e.g. "c0:f5:35:ed:f5:f1"
    std::string normalized_mac;    // e.g. "c0f535edf5f1"
    std::string serial_number;     // e.g. "DEVICE_UUID_C0F535EDF5F1"
    std::string board_model;       // e.g. "Orange Pi 5 Max (RK3588)"
    std::string firmware_version;  // e.g. "1.1.0"
};

/**
 * @brief Generic key-value configuration entry.
 */
struct SENZR_API ConfigProperty {
    std::string key;
    std::string value;
    std::string description;
};

} // namespace v1
} // namespace senzr
