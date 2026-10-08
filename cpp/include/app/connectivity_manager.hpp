#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace app {

struct WifiNetwork {
    std::string ssid;
    int signal_strength{0};
    std::string security;
    bool in_use{false};
};

struct WifiStatus {
    bool connected{false};
    std::string ssid;
    std::string ip_address;
    std::string mac_address;
    int signal_strength{0};
};

struct EthernetStatus {
    bool connected{false};
    std::string interface_name;
    std::string ip_address;
    std::string prefix_cidr{"24"};
    std::string netmask{"255.255.255.0"};
    std::string gateway;
    std::string dns;
    std::string mode{"dhcp"}; // "dhcp" or "static"
    std::string mac_address;
    int speed_mbps{0};
    std::string duplex;
};

struct BluetoothDevice {
    std::string address;
    std::string name;
    int rssi{0};
    bool paired{false};
    bool connected{false};
};

struct BluetoothStatus {
    bool enabled{false};
    bool discoverable{false};
    std::string adapter_name;
    std::string mac_address;
    std::vector<BluetoothDevice> connected_devices;
};

class ConnectivityManager {
public:
    ConnectivityManager();

    // ── Wi-Fi Management ───────────────────────────────────────────────────────
    WifiStatus get_wifi_status() const;
    std::vector<WifiNetwork> scan_wifi_networks() const;
    bool connect_wifi(const std::string& ssid, const std::string& password, std::string& error_msg);
    bool disconnect_wifi();

    // ── Ethernet Management ────────────────────────────────────────────────────
    EthernetStatus get_ethernet_status() const;
    nlohmann::json ethernet_status_json() const;
    bool set_ethernet_dhcp(const std::string& interface_name, std::string& error_msg);
    bool set_ethernet_static_ip(const std::string& interface_name,
                                const std::string& ip_with_prefix,
                                const std::string& gateway,
                                const std::string& dns,
                                std::string& error_msg);

    // ── Bluetooth Management ───────────────────────────────────────────────────
    BluetoothStatus get_bluetooth_status() const;
    bool set_bluetooth_power(bool enable);
    bool set_bluetooth_discoverable(bool enable);
    std::vector<BluetoothDevice> scan_bluetooth_devices(int scan_seconds = 5) const;
    bool pair_bluetooth_device(const std::string& address, std::string& error_msg);

    // ── Network Failover & Autonomous Rescue Hotspot (Module 3) ───────────────
    bool configure_failover_metrics();
    bool check_wan_connectivity() const;
    void start_wan_monitor(int check_interval_sec = 10, int rescue_timeout_sec = 60);
    void stop_wan_monitor();
    bool is_hotspot_active() const { return hotspot_active_.load(); }
    bool toggle_hotspot(bool enable, const std::string& ssid = "SenzrOS-Rescue-AP", const std::string& psk = "senzr12345");

    // JSON serialization helpers
    nlohmann::json wifi_status_json() const;
    nlohmann::json wifi_scan_json() const;
    nlohmann::json bluetooth_status_json() const;
    nlohmann::json bluetooth_scan_json() const;
    nlohmann::json network_health_json() const;

private:
    static std::string exec_cmd(const std::string& cmd);
    static std::string get_nm_connection_for_iface(const std::string& iface);
    void wan_monitor_loop();

    std::atomic<bool> wan_monitor_running_{false};
    std::atomic<bool> wan_online_{true};
    std::atomic<bool> hotspot_active_{false};
    int check_interval_sec_{10};
    int rescue_timeout_sec_{60};
    std::thread wan_thread_;
};

} // namespace app
