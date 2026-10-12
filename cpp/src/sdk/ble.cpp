#include "senzr/ble.hpp"
#include "app/ble_access_manager.hpp"

namespace senzr::ble {

struct Manager::Impl {
    app::BleAccessManager& backend{app::BleAccessManager::instance()};
};

Manager::Manager() : pimpl_(std::make_unique<Impl>()) {}
Manager::~Manager() = default;

Manager& Manager::get_instance() {
    static Manager inst;
    return inst;
}

Result<void> Manager::initialize(const BleConfig& /*config*/) {
    if (pimpl_->backend.start()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to initialize BLE subsystem");
}

Result<void> Manager::start_advertising() {
    if (pimpl_->backend.start()) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to start BLE advertising");
}

Result<void> Manager::stop_advertising() {
    pimpl_->backend.stop();
    return Result<void>::success();
}

BleStatus Manager::get_status() const {
    BleStatus st;
    bool active = pimpl_->backend.is_running();
#if defined(__linux__)
    if (!active) {
        int rc = std::system("pgrep -f 'senzr_ble_peripheral.py' >/dev/null 2>&1 || pgrep -f 'edge-gateway-backend' >/dev/null 2>&1");
        if (rc == 0) {
            active = true;
        }
    }
#endif
    st.is_advertising = active;
    st.is_connected   = active;
    return st;
}

bool Manager::is_advertising() const {
    return get_status().is_advertising;
}

bool Manager::is_connected() const {
    return get_status().is_connected;
}

} // namespace senzr::ble
