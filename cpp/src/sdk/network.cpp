#include "senzr/network.hpp"
#include "app/connectivity_manager.hpp"

namespace senzr::net {

struct Manager::Impl {
    app::ConnectivityManager backend;
};

Manager::Manager() : pimpl_(std::make_unique<Impl>()) {}
Manager::~Manager() = default;

Manager& Manager::get_instance() {
    static Manager inst;
    return inst;
}

Result<EthernetStatus> Manager::get_ethernet_status(const std::string& /*iface*/) {
    auto app_st = pimpl_->backend.get_ethernet_status();
    EthernetStatus st;
    st.connected = app_st.connected;
    st.ip_address = app_st.ip_address;
    st.interface_name = app_st.interface_name;
    st.mac_address = app_st.mac_address;
    st.gateway = app_st.gateway;
    st.subnet_mask = app_st.netmask;
    st.dns = app_st.dns;
    return st;
}

Result<WifiStatus> Manager::get_wifi_status(const std::string& /*iface*/) {
    auto app_st = pimpl_->backend.get_wifi_status();
    WifiStatus st;
    st.connected = app_st.connected;
    st.ssid = app_st.ssid;
    st.ip_address = app_st.ip_address;
    st.signal_strength_dbm = app_st.signal_strength;
    st.is_ap_mode = pimpl_->backend.is_hotspot_active();
    return st;
}

Result<std::vector<WifiNetwork>> Manager::scan_wifi() {
    auto app_nets = pimpl_->backend.scan_wifi_networks();
    std::vector<WifiNetwork> res;
    res.reserve(app_nets.size());
    for (const auto& n : app_nets) {
        WifiNetwork net;
        net.ssid = n.ssid;
        net.signal_strength = n.signal_strength;
        net.security = n.security;
        net.in_use = n.in_use;
        res.push_back(net);
    }
    return res;
}

Result<void> Manager::connect_wifi(const std::string& ssid, const std::string& password) {
    std::string err_msg;
    if (pimpl_->backend.connect_wifi(ssid, password, err_msg)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, err_msg.empty() ? ("Failed to connect to Wi-Fi SSID: " + ssid) : err_msg);
}

Result<void> Manager::disconnect_wifi() {
    if (pimpl_->backend.disconnect_wifi()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, "Failed to disconnect from Wi-Fi");
}

Result<void> Manager::start_hotspot(const std::string& ssid, const std::string& password) {
    if (pimpl_->backend.toggle_hotspot(true, ssid, password.empty() ? "senzr12345" : password)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, "Failed to enable hotspot mode");
}

Result<void> Manager::stop_hotspot() {
    if (pimpl_->backend.toggle_hotspot(false)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::NetworkError, "Failed to disable hotspot mode");
}

bool Manager::is_hotspot_active() const {
    return pimpl_->backend.is_hotspot_active();
}

} // namespace senzr::net
