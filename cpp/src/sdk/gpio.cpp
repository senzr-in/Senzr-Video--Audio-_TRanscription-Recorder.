#include "senzr/gpio.hpp"
#include "app/gpio_manager.hpp"

namespace senzr::gpio {

struct Manager::Impl {
    app::GpioManager& backend{app::GpioManager::instance()};
};

Manager::Manager() : pimpl_(std::make_unique<Impl>()) {}
Manager::~Manager() = default;

Manager& Manager::get_instance() {
    static Manager inst;
    return inst;
}

Result<void> Manager::pin_mode(int physical_pin, PinMode mode) {
    auto app_mode = (mode == PinMode::Output) ? app::PinMode::Output : app::PinMode::Input;
    if (pimpl_->backend.pin_mode(physical_pin, app_mode)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to set pin mode for pin " + std::to_string(physical_pin));
}

Result<void> Manager::digital_write(int physical_pin, PinValue value) {
    auto app_val = (value == PinValue::High) ? app::PinValue::High : app::PinValue::Low;
    if (pimpl_->backend.digital_write(physical_pin, app_val)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to write digital value to pin " + std::to_string(physical_pin));
}

Result<PinValue> Manager::digital_read(int physical_pin) {
    auto val = pimpl_->backend.digital_read(physical_pin);
    return (val == app::PinValue::High) ? PinValue::High : PinValue::Low;
}

Result<void> Manager::toggle(int physical_pin) {
    if (pimpl_->backend.toggle(physical_pin)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to toggle pin " + std::to_string(physical_pin));
}

Result<void> Manager::define_alias(const std::string& alias, int physical_pin) {
    if (pimpl_->backend.define_alias(alias, physical_pin)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::InvalidArgument, "Invalid pin or alias: " + alias);
}

Result<void> Manager::write_alias(const std::string& alias, PinValue value) {
    auto app_val = (value == PinValue::High) ? app::PinValue::High : app::PinValue::Low;
    if (pimpl_->backend.write_alias(alias, app_val)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::InvalidArgument, "Failed to write to alias: " + alias);
}

Result<PinValue> Manager::read_alias(const std::string& alias) {
    auto val = pimpl_->backend.read_alias(alias);
    return (val == app::PinValue::High) ? PinValue::High : PinValue::Low;
}

Result<void> Manager::pulse(int physical_pin, int duration_ms, PinValue active_level) {
    auto app_level = (active_level == PinValue::High) ? app::PinValue::High : app::PinValue::Low;
    if (pimpl_->backend.pulse(physical_pin, duration_ms, app_level)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to trigger pulse on pin " + std::to_string(physical_pin));
}

Result<void> Manager::pulse_alias(const std::string& alias, int duration_ms, PinValue active_level) {
    auto app_level = (active_level == PinValue::High) ? app::PinValue::High : app::PinValue::Low;
    if (pimpl_->backend.pulse_alias(alias, duration_ms, app_level)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::InvalidArgument, "Failed to trigger pulse on alias: " + alias);
}

Result<void> Manager::cancel_pulse(int physical_pin) {
    if (pimpl_->backend.cancel_pulse(physical_pin)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to cancel pulse on pin " + std::to_string(physical_pin));
}

Result<void> Manager::buzzer_beep(int physical_pin, int count, int on_ms, int off_ms) {
    if (pimpl_->backend.buzzer_beep(physical_pin, count, on_ms, off_ms)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to trigger buzzer beep on pin " + std::to_string(physical_pin));
}

Result<void> Manager::emergency_disarm(int relay_pin) {
    if (pimpl_->backend.emergency_disarm(relay_pin)) {
        return Result<void>::success();
    }
    return Result<void>(ErrorCode::HardwareError, "Failed to emergency disarm relay pin " + std::to_string(relay_pin));
}

void Manager::start_safety_monitor(int safety_sensor_pin, int relay_pin) {
    pimpl_->backend.start_safety_monitor(safety_sensor_pin, relay_pin);
}

void Manager::stop_safety_monitor() {
    pimpl_->backend.stop_safety_monitor();
}

bool Manager::is_safety_tripped() const {
    return pimpl_->backend.is_safety_tripped();
}

Result<PinValue> Manager::digital_read_debounced(int physical_pin, int debounce_ms) {
    auto val = pimpl_->backend.digital_read_debounced(physical_pin, debounce_ms);
    return (val == app::PinValue::High) ? PinValue::High : PinValue::Low;
}

std::vector<PinInfo> Manager::get_all_pins() const {
    auto app_pins = pimpl_->backend.get_all_pins();
    std::vector<PinInfo> res;
    res.reserve(app_pins.size());
    for (const auto& p : app_pins) {
        PinInfo info;
        info.physical_pin = p.physical_pin;
        info.kernel_gpio  = p.kernel_gpio;
        info.name         = p.name;
        info.default_role = p.default_role;
        info.mode         = (p.mode == app::PinMode::Output) ? PinMode::Output : PinMode::Input;
        info.value        = (p.value == app::PinValue::High) ? PinValue::High : PinValue::Low;
        info.exported     = p.exported;
        info.pin_number   = p.physical_pin;
        info.alias        = p.default_role;
        info.state        = info.value;
        res.push_back(info);
    }
    return res;
}

std::optional<PinInfo> Manager::get_pin_info(int physical_pin) const {
    auto p = pimpl_->backend.get_pin_info(physical_pin);
    if (!p) return std::nullopt;
    PinInfo info;
    info.physical_pin = p->physical_pin;
    info.kernel_gpio  = p->kernel_gpio;
    info.name         = p->name;
    info.default_role = p->default_role;
    info.mode         = (p->mode == app::PinMode::Output) ? PinMode::Output : PinMode::Input;
    info.value        = (p->value == app::PinValue::High) ? PinValue::High : PinValue::Low;
    info.exported     = p->exported;
    info.pin_number   = p->physical_pin;
    info.alias        = p->default_role;
    info.state        = info.value;
    return info;
}

} // namespace senzr::gpio
