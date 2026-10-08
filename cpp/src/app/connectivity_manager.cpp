#include "app/connectivity_manager.hpp"
#include <iostream>
#include <sstream>
#include <array>
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>

namespace app {

using json = nlohmann::json;

ConnectivityManager::ConnectivityManager() = default;

std::string ConnectivityManager::exec_cmd(const std::string& cmd) {
    std::array<char, 256> buffer{};
    std::string result;
#ifdef _WIN32
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// ── Wi-Fi Implementations ───────────────────────────────────────────────────

WifiStatus ConnectivityManager::get_wifi_status() const {
    WifiStatus status;
    std::string ssid = exec_cmd("iwgetid -r 2>/dev/null");
    ssid.erase(std::remove(ssid.begin(), ssid.end(), '\n'), ssid.end());
    ssid.erase(std::remove(ssid.begin(), ssid.end(), '\r'), ssid.end());

    // Fallback 1: check nmcli connection on wlan0
    if (ssid.empty()) {
        std::string nm_conn = exec_cmd("nmcli -t -f GENERAL.CONNECTION dev show wlan0 2>/dev/null");
        auto pos = nm_conn.find(':');
        if (pos != std::string::npos) {
            ssid = nm_conn.substr(pos + 1);
            ssid.erase(std::remove(ssid.begin(), ssid.end(), '\n'), ssid.end());
            ssid.erase(std::remove(ssid.begin(), ssid.end(), '\r'), ssid.end());
        }
    }

    // Fallback 2: parse nmcli dev wifi list for in-use marker (* or yes)
    if (ssid.empty() || ssid == "--") {
        std::string output = exec_cmd("nmcli -t -f in-use,ssid,signal dev wifi 2>/dev/null");
        std::istringstream stream(output);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("*:", 0) == 0 || line.rfind("yes:", 0) == 0 || line.rfind("true:", 0) == 0) {
                std::stringstream ss(line);
                std::string in_use, s, sig;
                std::getline(ss, in_use, ':');
                std::getline(ss, s, ':');
                std::getline(ss, sig, ':');
                ssid = s;
                try { status.signal_strength = std::stoi(sig); } catch (...) {}
                break;
            }
        }
    }

    if (!ssid.empty() && ssid != "--") {
        status.connected = true;
        status.ssid = ssid;
    }

    // Query IP Address
    std::string ip_out = exec_cmd("hostname -I 2>/dev/null");
    std::istringstream ip_ss(ip_out);
    ip_ss >> status.ip_address;
    if (status.connected && status.signal_strength == 0) {
        // Query real wireless link quality from Linux kernel
        std::string p_out = exec_cmd("awk 'NR==3 {print int($3 * 100 / 70)}' /proc/net/wireless 2>/dev/null");
        try { status.signal_strength = std::clamp(std::stoi(p_out), 0, 100); } catch (...) { status.signal_strength = 0; }
    }

    // Query MAC Address
    std::string mac_out = exec_cmd("cat /sys/class/net/wlan0/address 2>/dev/null");
    mac_out.erase(std::remove(mac_out.begin(), mac_out.end(), '\n'), mac_out.end());
    mac_out.erase(std::remove(mac_out.begin(), mac_out.end(), '\r'), mac_out.end());
    status.mac_address = mac_out;

    return status;
}

std::vector<WifiNetwork> ConnectivityManager::scan_wifi_networks() const {
    std::vector<WifiNetwork> networks;
    std::string output = exec_cmd("nmcli -t -f in-use,ssid,signal,security dev wifi list --rescan yes 2>/dev/null");
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string in_use, ssid, signal, security;
        std::getline(ss, in_use, ':');
        std::getline(ss, ssid, ':');
        std::getline(ss, signal, ':');
        std::getline(ss, security, ':');

        if (ssid.empty()) continue;

        // Deduplicate SSIDs
        auto it = std::find_if(networks.begin(), networks.end(),
                               [&](const WifiNetwork& w) { return w.ssid == ssid; });
        if (it == networks.end()) {
            WifiNetwork net;
            net.ssid = ssid;
            net.in_use = (in_use == "*");
            try { net.signal_strength = std::stoi(signal); } catch (...) { net.signal_strength = 0; }
            net.security = security.empty() ? "Open" : security;
            networks.push_back(net);
        }
    }
    return networks;
}

bool ConnectivityManager::connect_wifi(const std::string& ssid, const std::string& password, std::string& error_msg) {
    std::string cmd;
    if (password.empty()) {
        cmd = "nmcli dev wifi connect \"" + ssid + "\" 2>&1";
    } else {
        cmd = "nmcli dev wifi connect \"" + ssid + "\" password \"" + password + "\" 2>&1";
    }

    std::string output = exec_cmd(cmd);
    if (output.find("successfully activated") != std::string::npos ||
        output.find("Connection successfully") != std::string::npos) {
        return true;
    }

    error_msg = output;
    return false;
}

bool ConnectivityManager::disconnect_wifi() {
    std::string output = exec_cmd("nmcli dev disconnect wlan0 2>&1");
    return output.find("successfully disconnected") != std::string::npos;
}

// Helper to find connection name for an interface in NetworkManager
std::string ConnectivityManager::get_nm_connection_for_iface(const std::string& iface) {
    std::string out = exec_cmd("nmcli -t -f GENERAL.CONNECTION dev show " + iface + " 2>/dev/null");
    auto pos = out.find(':');
    std::string conn_name;
    if (pos != std::string::npos) {
        conn_name = out.substr(pos + 1);
        conn_name.erase(std::remove(conn_name.begin(), conn_name.end(), '\n'), conn_name.end());
        conn_name.erase(std::remove(conn_name.begin(), conn_name.end(), '\r'), conn_name.end());
    }
    if (conn_name.empty() || conn_name == "--") {
        std::string con_list = exec_cmd("nmcli -t -f NAME,DEVICE con show 2>/dev/null");
        std::istringstream iss(con_list);
        std::string line;
        while (std::getline(iss, line)) {
            auto colon = line.find_last_of(':');
            if (colon != std::string::npos && line.substr(colon + 1) == iface) {
                conn_name = line.substr(0, colon);
                break;
            }
        }
    }
    return conn_name;
}

// ── Ethernet Implementations ────────────────────────────────────────────────

EthernetStatus ConnectivityManager::get_ethernet_status() const {
    EthernetStatus status;
    
    // Find active ethernet interface (eth0, eth1, enP3p49s0, end0)
    std::string ifaces_out = exec_cmd("ls /sys/class/net/ 2>/dev/null");
    std::istringstream stream(ifaces_out);
    std::string iface;
    std::string target_iface;

    while (stream >> iface) {
        if ((iface.rfind("eth", 0) == 0 || iface.rfind("en", 0) == 0 || iface.rfind("end", 0) == 0) &&
            iface.find("wlan") == std::string::npos) {
            target_iface = iface;
            // Prefer interface with carrier=1 if multiple
            std::string carrier = exec_cmd("cat /sys/class/net/" + iface + "/carrier 2>/dev/null");
            if (carrier.find('1') != std::string::npos) {
                break;
            }
        }
    }

    if (target_iface.empty()) {
        target_iface = "eth0";
    }

    status.interface_name = target_iface;

    // Check cable carrier status
    std::string carrier = exec_cmd("cat /sys/class/net/" + target_iface + "/carrier 2>/dev/null");
    status.connected = (carrier.find('1') != std::string::npos);

    // Query MAC address
    std::string mac = exec_cmd("cat /sys/class/net/" + target_iface + "/address 2>/dev/null");
    mac.erase(std::remove(mac.begin(), mac.end(), '\n'), mac.end());
    mac.erase(std::remove(mac.begin(), mac.end(), '\r'), mac.end());
    status.mac_address = mac;

    // Query Link Speed & Duplex
    if (status.connected) {
        std::string speed = exec_cmd("cat /sys/class/net/" + target_iface + "/speed 2>/dev/null");
        try { status.speed_mbps = std::stoi(speed); } catch (...) { status.speed_mbps = 0; }
        
        std::string duplex = exec_cmd("cat /sys/class/net/" + target_iface + "/duplex 2>/dev/null");
        duplex.erase(std::remove(duplex.begin(), duplex.end(), '\n'), duplex.end());
        duplex.erase(std::remove(duplex.begin(), duplex.end(), '\r'), duplex.end());
        status.duplex = duplex.empty() ? "Full" : duplex;
    }

    // Query IP Address and Prefix (e.g. 10.50.53.189/24)
    std::string ip_cidr = exec_cmd("ip -4 -o addr show " + target_iface + " 2>/dev/null | awk '{print $4}'");
    ip_cidr.erase(std::remove(ip_cidr.begin(), ip_cidr.end(), '\n'), ip_cidr.end());
    ip_cidr.erase(std::remove(ip_cidr.begin(), ip_cidr.end(), '\r'), ip_cidr.end());
    
    if (!ip_cidr.empty()) {
        auto slash_pos = ip_cidr.find('/');
        if (slash_pos != std::string::npos) {
            status.ip_address = ip_cidr.substr(0, slash_pos);
            status.prefix_cidr = ip_cidr.substr(slash_pos + 1);
        } else {
            status.ip_address = ip_cidr;
        }
    } else {
        status.ip_address = "-";
    }

    // Query Gateway
    std::string gw = exec_cmd("ip route show default dev " + target_iface + " 2>/dev/null | awk '{print $3}'");
    gw.erase(std::remove(gw.begin(), gw.end(), '\n'), gw.end());
    gw.erase(std::remove(gw.begin(), gw.end(), '\r'), gw.end());
    if (gw.empty()) {
        gw = exec_cmd("ip route show default 2>/dev/null | awk '{print $3}' | head -n 1");
        gw.erase(std::remove(gw.begin(), gw.end(), '\n'), gw.end());
        gw.erase(std::remove(gw.begin(), gw.end(), '\r'), gw.end());
    }
    status.gateway = gw.empty() ? "-" : gw;

    // Query DNS
    std::string dns = exec_cmd("nmcli -t -f IP4.DNS dev show " + target_iface + " 2>/dev/null");
    auto dns_pos = dns.find(':');
    if (dns_pos != std::string::npos) {
        dns = dns.substr(dns_pos + 1);
        dns.erase(std::remove(dns.begin(), dns.end(), '\n'), dns.end());
        dns.erase(std::remove(dns.begin(), dns.end(), '\r'), dns.end());
    }
    if (dns.empty()) {
        dns = exec_cmd("cat /etc/resolv.conf 2>/dev/null | grep nameserver | awk '{print $2}' | head -n 1");
        dns.erase(std::remove(dns.begin(), dns.end(), '\n'), dns.end());
        dns.erase(std::remove(dns.begin(), dns.end(), '\r'), dns.end());
    }
    status.dns = dns.empty() ? "8.8.8.8" : dns;

    // Query IP Mode (dhcp / static)
    std::string conn_name = get_nm_connection_for_iface(target_iface);
    if (!conn_name.empty()) {
        std::string method = exec_cmd("nmcli -t -f ipv4.method con show \"" + conn_name + "\" 2>/dev/null");
        if (method.find("manual") != std::string::npos) {
            status.mode = "static";
        } else {
            status.mode = "dhcp";
        }
    } else {
        status.mode = "dhcp";
    }

    return status;
}

nlohmann::json ConnectivityManager::ethernet_status_json() const {
    auto eth = get_ethernet_status();
    return {
        {"connected",      eth.connected},
        {"interface_name", eth.interface_name},
        {"ip_address",     eth.ip_address},
        {"prefix_cidr",    eth.prefix_cidr},
        {"gateway",        eth.gateway},
        {"dns",            eth.dns},
        {"mode",           eth.mode},
        {"mac_address",    eth.mac_address},
        {"speed_mbps",     eth.speed_mbps},
        {"duplex",         eth.duplex}
    };
}

bool ConnectivityManager::set_ethernet_dhcp(const std::string& interface_name, std::string& error_msg) {
    std::string target_iface = interface_name.empty() ? get_ethernet_status().interface_name : interface_name;
    if (target_iface.empty()) target_iface = "eth0";

    std::string conn_name = get_nm_connection_for_iface(target_iface);
    std::string cmd;
    if (!conn_name.empty()) {
        cmd = "nmcli con mod \"" + conn_name + "\" ipv4.method auto ipv4.addresses \"\" ipv4.gateway \"\" ipv4.dns \"\" 2>&1 && nmcli con up \"" + conn_name + "\" 2>&1";
    } else {
        cmd = "nmcli dev modify " + target_iface + " ipv4.method auto 2>&1";
    }

    std::string output = exec_cmd(cmd);
    if (output.find("successfully") != std::string::npos || output.find("Connection successfully") != std::string::npos || output.empty()) {
        return true;
    }
    error_msg = output;
    return false;
}

bool ConnectivityManager::set_ethernet_static_ip(const std::string& interface_name,
                                                const std::string& ip_with_prefix,
                                                const std::string& gateway,
                                                const std::string& dns,
                                                std::string& error_msg) {
    std::string target_iface = interface_name.empty() ? get_ethernet_status().interface_name : interface_name;
    if (target_iface.empty()) target_iface = "eth0";

    std::string full_ip = ip_with_prefix;
    if (full_ip.find('/') == std::string::npos) {
        full_ip += "/24";
    }

    std::string conn_name = get_nm_connection_for_iface(target_iface);
    if (conn_name.empty()) {
        exec_cmd("nmcli con add type ethernet ifname " + target_iface + " con-name \"" + target_iface + "\" 2>/dev/null");
        conn_name = target_iface;
    }

    std::string mod_cmd = "nmcli con mod \"" + conn_name + "\" ipv4.method manual ipv4.addresses \"" + full_ip + "\"";
    if (!gateway.empty() && gateway != "-") {
        mod_cmd += " ipv4.gateway \"" + gateway + "\"";
    } else {
        mod_cmd += " ipv4.gateway \"\"";
    }
    if (!dns.empty()) {
        mod_cmd += " ipv4.dns \"" + dns + "\"";
    }
    mod_cmd += " 2>&1 && nmcli con up \"" + conn_name + "\" 2>&1";

    std::string output = exec_cmd(mod_cmd);
    if (output.find("successfully") != std::string::npos || output.find("Connection successfully") != std::string::npos) {
        return true;
    }
    error_msg = output;
    return false;
}

// ── Bluetooth Implementations ───────────────────────────────────────────────

BluetoothStatus ConnectivityManager::get_bluetooth_status() const {
    BluetoothStatus status;
    std::string output = exec_cmd("bluetoothctl show 2>/dev/null");
    
    if (output.empty() || output.find("No default controller") != std::string::npos) {
        exec_cmd("rfkill unblock bluetooth 2>/dev/null; hciconfig hci0 up 2>/dev/null");
        output = exec_cmd("bluetoothctl show 2>/dev/null");
    }

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("Powered: yes") != std::string::npos) status.enabled = true;
        if (line.find("Discoverable: yes") != std::string::npos) status.discoverable = true;
        if (line.find("Name:") != std::string::npos) {
            auto pos = line.find("Name:");
            status.adapter_name = line.substr(pos + 5);
            auto s = status.adapter_name.find_first_not_of(" \t\r\n");
            if (s != std::string::npos) status.adapter_name = status.adapter_name.substr(s);
        }
        if (line.find("Controller ") != std::string::npos) {
            std::stringstream ss(line);
            std::string prefix, mac;
            ss >> prefix >> mac;
            status.mac_address = mac;
        }
    }

    if (status.adapter_name.empty()) {
        status.adapter_name = "-";
    }

    // Query paired devices
    std::string paired_out = exec_cmd("bluetoothctl devices Paired 2>/dev/null");
    std::istringstream p_stream(paired_out);
    while (std::getline(p_stream, line)) {
        if (line.rfind("Device ", 0) == 0) {
            std::stringstream ss(line);
            std::string tag, mac, name;
            ss >> tag >> mac;
            std::getline(ss, name);
            name.erase(0, name.find_first_not_of(" \t\r\n"));
            BluetoothDevice dev;
            dev.address = mac;
            dev.name = name.empty() ? "Paired Device" : name;
            dev.paired = true;
            status.connected_devices.push_back(dev);
        }
    }

    return status;
}

bool ConnectivityManager::set_bluetooth_power(bool enable) {
    std::string cmd;
    if (enable) {
        cmd = "rfkill unblock bluetooth 2>/dev/null; hciconfig hci0 up 2>/dev/null; bluetoothctl power on 2>&1";
    } else {
        cmd = "bluetoothctl power off 2>&1";
    }
    std::string output = exec_cmd(cmd);
    return output.find("succeeded") != std::string::npos || output.find("success") != std::string::npos || enable;
}

bool ConnectivityManager::set_bluetooth_discoverable(bool enable) {
    std::string cmd = enable ? "bluetoothctl discoverable on" : "bluetoothctl discoverable off";
    std::string output = exec_cmd(cmd);
    return output.find("succeeded") != std::string::npos || output.find("success") != std::string::npos;
}

std::vector<BluetoothDevice> ConnectivityManager::scan_bluetooth_devices(int scan_seconds) const {
    std::vector<BluetoothDevice> devices;

    // 1. Ensure Bluetooth is unblocked and powered up
    exec_cmd("rfkill unblock bluetooth 2>/dev/null; hciconfig hci0 up 2>/dev/null; bluetoothctl power on 2>/dev/null");

    // 2. Trigger active Bluetooth discovery and capture live output
    std::string scan_cmd = "timeout " + std::to_string(scan_seconds) + " bluetoothctl scan on 2>&1";
    std::string scan_output = exec_cmd(scan_cmd);

    // 3. Query all discovered devices from BlueZ cache
    std::string devices_output = exec_cmd("bluetoothctl devices 2>/dev/null");
    
    std::string combined = scan_output + "\n" + devices_output;
    std::istringstream stream(combined);
    std::string line;

    while (std::getline(stream, line)) {
        auto dev_pos = line.find("Device ");
        if (dev_pos != std::string::npos) {
            std::string sub = line.substr(dev_pos + 7);
            std::stringstream ss(sub);
            std::string mac, name;
            ss >> mac;
            std::getline(ss, name);
            
            auto start = name.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) {
                name = name.substr(start);
            } else {
                name = "Bluetooth Device";
            }

            // Filter out internal telemetry tokens
            if (name.find("RSSI") == std::string::npos &&
                name.find("TxPower") == std::string::npos &&
                name.find("ManufacturerData") == std::string::npos &&
                mac.find(':') != std::string::npos &&
                mac.length() >= 17) {
                
                auto it = std::find_if(devices.begin(), devices.end(), [&](const BluetoothDevice& d) {
                    return d.address == mac;
                });
                
                if (it == devices.end()) {
                    BluetoothDevice dev;
                    dev.address = mac;
                    dev.name = name;
                    devices.push_back(dev);
                } else if (it->name == "Bluetooth Device" && name != "Bluetooth Device") {
                    it->name = name;
                }
            }
        }
    }
    return devices;
}

bool ConnectivityManager::pair_bluetooth_device(const std::string& address, std::string& error_msg) {
    std::string cmd = "bluetoothctl pair " + address + " 2>&1";
    std::string output = exec_cmd(cmd);
    if (output.find("Pairing successful") != std::string::npos) {
        exec_cmd("bluetoothctl trust " + address);
        exec_cmd("bluetoothctl connect " + address);
        return true;
    }
    error_msg = output;
    return false;
}

// ── JSON Serialization ──────────────────────────────────────────────────────

json ConnectivityManager::wifi_status_json() const {
    auto st = get_wifi_status();
    return {
        {"connected",       st.connected},
        {"ssid",            st.ssid},
        {"ip_address",      st.ip_address},
        {"mac_address",     st.mac_address},
        {"signal_strength", st.signal_strength}
    };
}

json ConnectivityManager::wifi_scan_json() const {
    auto nets = scan_wifi_networks();
    json list = json::array();
    for (const auto& n : nets) {
        list.push_back({
            {"ssid",            n.ssid},
            {"signal_strength", n.signal_strength},
            {"security",        n.security},
            {"in_use",          n.in_use}
        });
    }
    return {{"networks", list}, {"count", list.size()}};
}

json ConnectivityManager::bluetooth_status_json() const {
    auto bt = get_bluetooth_status();
    json devs = json::array();
    for (const auto& d : bt.connected_devices) {
        devs.push_back({
            {"address",   d.address},
            {"name",      d.name},
            {"paired",    d.paired},
            {"connected", d.connected}
        });
    }
    return {
        {"enabled",           bt.enabled},
        {"discoverable",      bt.discoverable},
        {"adapter_name",      bt.adapter_name},
        {"mac_address",       bt.mac_address},
        {"connected_devices", devs}
    };
}

json ConnectivityManager::bluetooth_scan_json() const {
    auto devs = scan_bluetooth_devices(3);
    json list = json::array();
    for (const auto& d : devs) {
        list.push_back({
            {"address", d.address},
            {"name",    d.name}
        });
    }
    return {{"devices", list}, {"count", list.size()}};
}

// ── Network Failover & Autonomous Rescue Hotspot (Module 3) ───────────────

bool ConnectivityManager::configure_failover_metrics() {
    // Set eth0 priority metric to 100, and wlan0 backup metric to 200
    std::string eth_conn = get_nm_connection_for_iface("eth0");
    if (!eth_conn.empty()) {
        exec_cmd("nmcli con mod \"" + eth_conn + "\" ipv4.route-metric 100 2>/dev/null");
    }

    std::string wifi_conn = get_nm_connection_for_iface("wlan0");
    if (!wifi_conn.empty()) {
        exec_cmd("nmcli con mod \"" + wifi_conn + "\" ipv4.route-metric 200 2>/dev/null");
    }
    return true;
}

bool ConnectivityManager::check_wan_connectivity() const {
#ifndef _WIN32
    std::string out = exec_cmd("ping -c 1 -W 2 1.1.1.1 2>/dev/null");
    if (out.find("1 received") != std::string::npos || out.find("1 packets received") != std::string::npos) {
        return true;
    }
    std::string dns_out = exec_cmd("ping -c 1 -W 2 8.8.8.8 2>/dev/null");
    return (dns_out.find("1 received") != std::string::npos || dns_out.find("1 packets received") != std::string::npos);
#else
    return true;
#endif
}

bool ConnectivityManager::toggle_hotspot(bool enable, const std::string& ssid, const std::string& psk) {
    if (enable) {
        std::string cmd = "nmcli dev wifi hotspot ifname wlan0 ssid \"" + ssid + "\" password \"" + psk + "\" 2>&1";
        std::string out = exec_cmd(cmd);
        bool success = (out.find("successfully") != std::string::npos || out.find("Device 'wlan0' successfully") != std::string::npos);
        hotspot_active_.store(success);
        return success;
    } else {
        std::string out = exec_cmd("nmcli con down Hotspot 2>&1 || nmcli dev disconnect wlan0 2>&1");
        hotspot_active_.store(false);
        return true;
    }
}

void ConnectivityManager::start_wan_monitor(int check_interval_sec, int rescue_timeout_sec) {
    if (wan_monitor_running_.load()) return;

    check_interval_sec_ = check_interval_sec;
    rescue_timeout_sec_ = rescue_timeout_sec;
    wan_monitor_running_.store(true);

    configure_failover_metrics();
    wan_thread_ = std::thread(&ConnectivityManager::wan_monitor_loop, this);
}

void ConnectivityManager::stop_wan_monitor() {
    if (wan_monitor_running_.load()) {
        wan_monitor_running_.store(false);
        if (wan_thread_.joinable()) {
            wan_thread_.join();
        }
    }
}

void ConnectivityManager::wan_monitor_loop() {
    int consecutive_failures = 0;
    const int threshold_cycles = rescue_timeout_sec_ / (check_interval_sec_ > 0 ? check_interval_sec_ : 10);

    while (wan_monitor_running_.load()) {
        bool online = check_wan_connectivity();
        wan_online_.store(online);

        if (!online) {
            consecutive_failures++;
            std::cerr << "[ConnectivityManager] WAN offline (" << consecutive_failures * check_interval_sec_ 
                      << "s / " << rescue_timeout_sec_ << "s)" << std::endl;

            // Trigger autonomous rescue hotspot fallback after WAN loss duration threshold
            if (consecutive_failures >= threshold_cycles && !hotspot_active_.load()) {
                std::cout << "[ConnectivityManager] WAN loss threshold reached! Activating Autonomous Rescue Hotspot..." << std::endl;
                toggle_hotspot(true);
            }
        } else {
            if (consecutive_failures >= threshold_cycles && hotspot_active_.load()) {
                std::cout << "[ConnectivityManager] WAN connectivity restored. Deactivating Rescue Hotspot..." << std::endl;
                toggle_hotspot(false);
            }
            consecutive_failures = 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds(check_interval_sec_));
    }
}

json ConnectivityManager::network_health_json() const {
    return {
        {"wan_online",         wan_online_.load()},
        {"hotspot_active",     hotspot_active_.load()},
        {"monitor_running",    wan_monitor_running_.load()},
        {"failover_metrics", {
            {"eth0", 100},
            {"wlan0", 200}
        }}
    };
}

} // namespace app
