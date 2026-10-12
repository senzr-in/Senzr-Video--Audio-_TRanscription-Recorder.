#pragma once

#include "senzr/types.hpp"
#include "senzr/error.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace senzr::gpio {

/**
 * @brief Pin direction configuration.
 */
enum class PinMode {
    Input  = 0,
    Output = 1
};

/**
 * @brief Digital logic voltage levels.
 */
enum class PinValue {
    Low  = 0,
    High = 1
};

using PinState = PinValue;

/**
 * @brief Information and state for a single hardware pin.
 */
struct PinInfo {
    int physical_pin{0};
    int kernel_gpio{0};
    std::string name;
    std::string default_role; // "RELAY", "BUZZER", "STATUS_LED", "DOOR_SENSOR", "GENERAL"
    PinMode mode{PinMode::Output};
    PinValue value{PinValue::Low};
    bool exported{false};

    // Backward-compatibility aliases
    int pin_number{0};
    std::string alias;
    PinState state{PinState::Low};
};

/**
 * @brief Thread-safe GPIO Manager for 40-pin header, relay pulsing, and safety beam monitoring.
 */
class Manager {
public:
    static Manager& get_instance();

    // ── Low-Level Pin Operations ──────────────────────────────────────────────
    Result<void> pin_mode(int physical_pin, PinMode mode);
    Result<void> digital_write(int physical_pin, PinValue value);
    Result<PinValue> digital_read(int physical_pin);
    Result<void> toggle(int physical_pin);

    // ── Named Pin Aliases ─────────────────────────────────────────────────────
    Result<void> define_alias(const std::string& alias, int physical_pin);
    Result<void> write_alias(const std::string& alias, PinValue value);
    Result<PinValue> read_alias(const std::string& alias);

    // ── High-Level Access Control Actions (Non-blocking) ──────────────────────
    Result<void> pulse(int physical_pin, int duration_ms = 3000, PinValue active_level = PinValue::High);
    Result<void> pulse_alias(const std::string& alias, int duration_ms = 3000, PinValue active_level = PinValue::High);
    Result<void> cancel_pulse(int physical_pin);
    Result<void> buzzer_beep(int physical_pin, int count = 2, int on_ms = 100, int off_ms = 100);

    // ── Safety Beam Interlock & Debounce ──────────────────────────────────────
    Result<void> emergency_disarm(int relay_pin = 7);
    void start_safety_monitor(int safety_sensor_pin = 15, int relay_pin = 7);
    void stop_safety_monitor();
    bool is_safety_tripped() const;
    Result<PinValue> digital_read_debounced(int physical_pin, int debounce_ms = 50);

    // ── Inspection & State Queries ────────────────────────────────────────────
    std::vector<PinInfo> get_all_pins() const;
    std::optional<PinInfo> get_pin_info(int physical_pin) const;

    // Backward compatibility helper
    static Manager& instance() { return get_instance(); }
    bool pulse_pin(int physical_pin, int duration_ms) {
        return pulse(physical_pin, duration_ms).has_value();
    }
    bool set_alias_state(const std::string& alias, PinValue val) {
        return write_alias(alias, val).has_value();
    }
    bool set_pin_state(int physical_pin, PinValue val) {
        return digital_write(physical_pin, val).has_value();
    }

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

} // namespace senzr::gpio
