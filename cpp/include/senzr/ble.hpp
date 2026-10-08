#pragma once

#include "senzr/export.hpp"
#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <memory>
#include <functional>

namespace senzr {
inline namespace v1 {
namespace ble {

/**
 * @brief Bluetooth Low Energy (BLE) peripheral configuration.
 */
struct SENZR_API BleConfig {
    std::string device_name{"Senzr-Edge"};
    std::string service_uuid{"0000ffff-0000-1000-8000-00805f9b34fb"};
    bool auto_advertise{true};
};

/**
 * @brief Operational state of BLE peripheral.
 */
struct SENZR_API BleStatus {
    bool is_advertising{false};
    bool is_connected{false};
    std::string peer_address;
    int rssi{0};
};

/**
 * @brief Thread-safe Bluetooth Low Energy (BLE) Access and Provisioning Manager.
 */
class SENZR_API Manager {
public:
    static Manager& get_instance();

    // ── BLE Lifecycle ─────────────────────────────────────────────────────────
    [[nodiscard]] Result<void> initialize(const BleConfig& config = {});
    [[nodiscard]] Result<void> start_advertising();
    [[nodiscard]] Result<void> stop_advertising();

    // ── State Queries ─────────────────────────────────────────────────────────
    [[nodiscard]] BleStatus get_status() const;
    [[nodiscard]] bool is_advertising() const;
    [[nodiscard]] bool is_connected() const;

    // Backward-compatibility alias
    static Manager& instance() { return get_instance(); }

    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) noexcept;
    Manager& operator=(Manager&&) noexcept;

private:
    Manager();

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

using AccessManager = Manager;

[[nodiscard]] inline Manager& get_manager() {
    return Manager::get_instance();
}

} // namespace ble
} // namespace v1
} // namespace senzr
