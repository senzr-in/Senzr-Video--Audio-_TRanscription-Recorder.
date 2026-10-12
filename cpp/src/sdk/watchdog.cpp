#include "senzr/watchdog.hpp"
#include "app/watchdog_supervisor.hpp"

namespace senzr::watchdog {

struct Supervisor::Impl {
    app::WatchdogSupervisor& backend{app::WatchdogSupervisor::instance()};
    int timeout_sec{10};
    bool active{false};
};

Supervisor::Supervisor() : pimpl_(std::make_unique<Impl>()) {}
Supervisor::~Supervisor() = default;

Supervisor& Supervisor::get_instance() {
    static Supervisor inst;
    return inst;
}

Result<void> Supervisor::start(int timeout_seconds) {
    pimpl_->timeout_sec = timeout_seconds;
    if (pimpl_->backend.start("/dev/watchdog", timeout_seconds)) {
        pimpl_->active = true;
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to start hardware watchdog supervisor");
}

Result<void> Supervisor::feed() {
    pimpl_->backend.heartbeat("main");
    return Result<void>::success();
}

Result<void> Supervisor::stop() {
    pimpl_->backend.stop();
    pimpl_->active = false;
    return Result<void>::success();
}

bool Supervisor::is_active() const noexcept {
    return pimpl_->active;
}

int Supervisor::get_timeout_seconds() const noexcept {
    return pimpl_->timeout_sec;
}

} // namespace senzr::watchdog
