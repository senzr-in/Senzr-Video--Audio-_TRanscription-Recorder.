#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>
#include <nlohmann/json.hpp>

namespace app {

enum class PinMode {
    INPUT  = 0,
    OUTPUT = 1,
    Input  = 0,
    Output = 1
};

enum class PinValue {
    LOW  = 0,
    HIGH = 1,
    Low  = 0,
    High = 1
};

using PinState = PinValue;

struct PinInfo {
    int physical_pin{0};
    int kernel_gpio{0};
    std::string name;
    std::string default_role; // "RELAY", "BUZZER", "STATUS_LED", "DOOR_SENSOR", "GENERAL"
    PinMode mode{PinMode::OUTPUT};
    PinValue value{PinValue::LOW};
    bool exported{false};

    // SDK compatibility aliases
    int pin_number{0};
    std::string alias;
    PinState state{PinState::Low};
};

class GpioManager {
public:
    GpioManager();
    ~GpioManager();

    static GpioManager& instance() {
        static GpioManager inst;
        return inst;
    }

    // ── Core Arduino-style API ────────────────────────────────────────────────
    bool pin_mode(int physical_pin, PinMode mode);
    bool digital_write(int physical_pin, PinValue value);
    PinValue digital_read(int physical_pin);
    bool toggle(int physical_pin);

    // ── Compatibility helpers for senzr::gpio SDK ─────────────────────────────
    bool pulse_pin(int physical_pin, int duration_ms) {
        return pulse(physical_pin, duration_ms);
    }
    bool set_alias_state(const std::string& alias, PinValue val) {
        return write_alias(alias, val);
    }
    bool set_pin_state(int physical_pin, PinValue val) {
        return digital_write(physical_pin, val);
    }

    // ── Named Alias API ("DOOR_RELAY", "BUZZER", "STATUS_LED", etc.) ─────────
    bool define_alias(const std::string& alias, int physical_pin);
    bool write_alias(const std::string& alias, PinValue value);
    PinValue read_alias(const std::string& alias);

    // ── High-Level Access Control Actions (Non-blocking) ──────────────────────
    // Pulse a pin (e.g. trigger door lock relay for N milliseconds, then auto-close)
    bool pulse(int physical_pin, int duration_ms = 3000, PinValue active_level = PinValue::HIGH);
    bool pulse_alias(const std::string& alias, int duration_ms = 3000, PinValue active_level = PinValue::HIGH);
    bool cancel_pulse(int physical_pin);

    // Beep a buzzer asynchronously (e.g. 2 beeps on door access granted)
    bool buzzer_beep(int physical_pin, int count = 2, int on_ms = 100, int off_ms = 100);
    bool buzzer_beep_alias(const std::string& alias, int count = 2, int on_ms = 100, int off_ms = 100);

    // ── Safety Beam Interlock & Debounce (Module 6.2 & 6.3) ───────────────────
    // Instantly abort any active relay pulse and disarm relay (< 5ms response)
    bool emergency_disarm(int relay_pin = 7);
    void start_safety_monitor(int safety_sensor_pin = 15, int relay_pin = 7);
    void stop_safety_monitor();
    bool is_safety_tripped() const;

    // Debounced digital read for noisy mechanical limit switches
    PinValue digital_read_debounced(int physical_pin, int debounce_ms = 50);

    // ── Status & Inspection ───────────────────────────────────────────────────
    std::vector<PinInfo> get_all_pins() const;
    std::optional<PinInfo> get_pin_info(int physical_pin) const;
    nlohmann::json pins_json() const;

    // Helper: convert standard 40-pin header physical pin to Linux kernel GPIO number (RK3588 SoC)
    // Applies to all RK3588-based boards: Orange Pi 5 Max, Rock 5B, NanoPC-T6, KHADAS Edge2, etc.
    static int physical_to_kernel_gpio(int physical_pin);
    static std::pair<std::string, int> kernel_gpio_to_chip_line(int kernel_gpio);

private:
    bool export_pin(int kernel_gpio);
    bool unexport_pin(int kernel_gpio);
    bool set_direction(int kernel_gpio, PinMode mode);
    bool set_value(int kernel_gpio, PinValue value);
    PinValue get_value(int kernel_gpio);

    mutable std::mutex mutex_;
    std::map<int, PinInfo> pins_;               // physical_pin -> PinInfo
    std::map<std::string, int> aliases_;        // alias name -> physical_pin

    // Safety Interlock & Active Pulse State
    std::atomic<bool> safety_monitor_running_{false};
    std::atomic<bool> safety_tripped_{false};
    std::map<int, std::atomic<bool>*> active_pulse_flags_;
    std::mutex pulse_mutex_;
    std::thread safety_thread_;
};

} // namespace app
