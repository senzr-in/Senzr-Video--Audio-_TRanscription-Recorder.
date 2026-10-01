#pragma once

#include "app/connectivity_manager.hpp"

namespace senzr::net {

using Manager = app::ConnectivityManager;
using WifiStatus = app::WifiStatus;
using EthernetStatus = app::EthernetStatus;
using WifiNetwork = app::WifiNetwork;

inline Manager& get_manager() {
    static Manager instance;
    return instance;
}

} // namespace senzr::net
