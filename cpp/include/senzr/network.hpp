#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <vector>
#include <memory>
#include <optional>

namespace senzr::net {

/**
 * @brief Current status of physical Ethernet network interfaces.
 */
struct EthernetStatus {
    bool connected{false};
    std::string ip_address;
    std::string interface_name{"eth0"};
    std::string mac_address;
    std::string gateway;
    std::string subnet_mask;
    std::string dns;
};

/**
 * @brief Current status of the Wi-Fi interface.
 */
struct WifiStatus {
    bool connected{false};
    std::string ssid;
    std::string ip_address;
    int signal_strength_dbm{0};
    bool is_ap_mode{false};
};

/**
 * @brief Information about a discovered Wi-Fi Access Point.
 */
struct WifiNetwork {
    std::string ssid;
    int signal_strength{0};
    std::string security;
    bool in_use{false};
};

/**
 * @brief Thread-safe Network Management API covering Ethernet, Wi-Fi Station, and Wi-Fi AP Hotspot modes.
 */
class Manager {
public:
    static Manager& get_instance();

    // ── Interface Status Queries ──────────────────────────────────────────────
    Result<EthernetStatus> get_ethernet_status(const std::string& iface = "eth0");
    Result<WifiStatus> get_wifi_status(const std::string& iface = "wlan0");

    // ── Wi-Fi Operations ──────────────────────────────────────────────────────
    Result<std::vector<WifiNetwork>> scan_wifi();
    Result<void> connect_wifi(const std::string& ssid, const std::string& password);
    Result<void> disconnect_wifi();

    // ── Local Hotspot / Captive Portal Provisioning ───────────────────────────
    Result<void> start_hotspot(const std::string& ssid = "Senzr-Edge-AP",
                               const std::string& password = "");
    Result<void> stop_hotspot();
    [[nodiscard]] bool is_hotspot_active() const;

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

inline Manager& get_manager() {
    return Manager::get_instance();
}

} // namespace senzr::net
