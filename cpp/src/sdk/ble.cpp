#include "senzr/ble.hpp"
#include "app/ble_access_manager.hpp"

namespace senzr {
inline namespace v1 {
namespace ble {

struct Manager::Impl {
    app::BleAccessManager& backend{app::BleAccessManager::instance()};
};

Manager::Manager() : pimpl_(std::make_unique<Impl>()) {}
Manager::~Manager() = default;
Manager::Manager(Manager&&) noexcept = default;
Manager& Manager::operator=(Manager&&) noexcept = default;

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
    st.is_advertising = pimpl_->backend.is_running();
    st.is_connected   = pimpl_->backend.is_running();
    return st;
}

bool Manager::is_advertising() const {
    return pimpl_->backend.is_running();
}

bool Manager::is_connected() const {
    return pimpl_->backend.is_running();
}

} // namespace ble
} // namespace v1
} // namespace senzr
