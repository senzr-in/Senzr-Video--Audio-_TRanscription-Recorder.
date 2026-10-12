#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <memory>
#include <functional>

namespace senzr::ble {

/**
 * @brief Bluetooth Low Energy (BLE) peripheral configuration.
 */
struct BleConfig {
    std::string device_name{"Senzr-Edge"};
    std::string service_uuid{"0000ffff-0000-1000-8000-00805f9b34fb"};
    bool auto_advertise{true};
};

/**
 * @brief Operational state of BLE peripheral.
 */
struct BleStatus {
    bool is_advertising{false};
    bool is_connected{false};
    std::string peer_address;
    int rssi{0};
};

/**
 * @brief Thread-safe Bluetooth Low Energy (BLE) Access and Provisioning Manager.
 */
class Manager {
public:
    static Manager& get_instance();

    // ── BLE Lifecycle ─────────────────────────────────────────────────────────
    Result<void> initialize(const BleConfig& config = {});
    Result<void> start_advertising();
    Result<void> stop_advertising();

    // ── State Queries ─────────────────────────────────────────────────────────
    [[nodiscard]] BleStatus get_status() const;
    [[nodiscard]] bool is_advertising() const;
    [[nodiscard]] bool is_connected() const;

    // Backward-compatibility alias
    static Manager& instance() { return get_instance(); }

private:
    Manager();
    ~Manager();
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

using AccessManager = Manager;

inline Manager& get_manager() {
    return Manager::get_instance();
}

} // namespace senzr::ble
