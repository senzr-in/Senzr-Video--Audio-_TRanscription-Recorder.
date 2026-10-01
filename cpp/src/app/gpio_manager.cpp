#include "app/gpio_manager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace app {

using json = nlohmann::json;

// ── Physical Pin to Linux Kernel GPIO Mapping: RK3588 SoC (40-pin header) ───
// Standard 40-pin layout used across RK3588 boards:
//   Orange Pi 5 Max, Rock 5B, NanoPC-T6, KHADAS Edge2, Firefly ITX-3588J, etc.
// GPIO numbering: (Bank * 32) + (Group * 8) + Index  | Group A=0, B=1, C=2, D=3
int GpioManager::physical_to_kernel_gpio(int physical_pin) {
    switch (physical_pin) {
        case 3:  return 13;  // GPIO0_B5 (I2C7_SDA)
        case 5:  return 14;  // GPIO0_B6 (I2C7_SCL)
        case 7:  return 20;  // GPIO0_C4 (Default: DOOR_RELAY)
        case 8:  return 13;  // GPIO0_B5 (UART)
        case 10: return 14;  // GPIO0_B6 (UART)
        case 11: return 60;  // GPIO1_D4 (Default: BUZZER)
        case 12: return 61;  // GPIO1_D5 (Default: LED_GREEN / STATUS_LED)
        case 13: return 62;  // GPIO1_D6 (Default: LED_RED / ERROR_LED)
        case 15: return 63;  // GPIO1_D7 (Default: DOOR_SENSOR)
        case 16: return 36;  // GPIO1_A4
        case 18: return 37;  // GPIO1_A5
        case 19: return 42;  // GPIO1_B2 (SPI)
        case 21: return 41;  // GPIO1_B1 (SPI)
        case 22: return 56;  // GPIO1_D0
        case 23: return 43;  // GPIO1_B3 (SPI)
        case 24: return 44;  // GPIO1_B4 (SPI)
        case 26: return 45;  // GPIO1_B5
        case 29: return 105; // GPIO3_B1
        case 31: return 106; // GPIO3_B2
        case 32: return 107; // GPIO3_B3
        case 33: return 108; // GPIO3_B4
        case 35: return 109; // GPIO3_B5
        case 36: return 110; // GPIO3_B6
        case 37: return 111; // GPIO3_B7
        case 38: return 148; // GPIO4_C4
        case 40: return 149; // GPIO4_C5
        default: return -1;  // pin not in map
    }
    // TODO: FET3588-C (Forlinx OK3588-C carrier board) GPIO pinout
    //       Ref: https://docs.forlinx.net — OK3588-C Hardware Manual, Section: GPIO/PinMux
    //       The FET3588-C uses 100-pin B2B connectors; carrier board exposes a subset.
    //       When the pinout table is available, add a runtime board-select here:
    //         if (board == "FET3588-C") -> use FET3588-C map
    //         else -> use standard RK3588 SBC 40-pin map (current default)
}

// ── Constructor ─────────────────────────────────────────────────────────────

GpioManager::GpioManager() {
    // Register common access control pins by default
    std::vector<int> default_pins = {7, 11, 12, 13, 15, 16, 18, 22, 29, 31, 32, 33, 35, 36, 37, 38, 40};
    
    for (int p : default_pins) {
        int k = physical_to_kernel_gpio(p);
        if (k > 0) {
            PinInfo info;
            info.physical_pin = p;
            info.kernel_gpio = k;
            info.name = "PIN_" + std::to_string(p);
            info.mode = PinMode::OUTPUT;
            info.value = PinValue::LOW;
            pins_[p] = info;
        }
    }

    // Assign standard access control aliases
    define_alias("DOOR_RELAY", 7);
    define_alias("RELAY", 7);
    define_alias("BUZZER", 11);
    define_alias("LED_GREEN", 12);
    define_alias("STATUS_LED", 12);
    define_alias("LED_RED", 13);
    define_alias("DOOR_SENSOR", 15);

    if (pins_.count(7))  pins_[7].default_role  = "RELAY";
    if (pins_.count(11)) pins_[11].default_role = "BUZZER";
    if (pins_.count(12)) pins_[12].default_role = "STATUS_LED";
    if (pins_.count(13)) pins_[13].default_role = "ERROR_LED";
    if (pins_.count(15)) {
        pins_[15].default_role = "DOOR_SENSOR";
        pins_[15].mode = PinMode::INPUT;
    }
}

GpioManager::~GpioManager() = default;

// ── Sysfs Kernel GPIO Low-Level Handlers ─────────────────────────────────────

bool GpioManager::export_pin(int kernel_gpio) {
#ifndef _WIN32
    std::string path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio);
    if (std::filesystem::exists(path)) {
        return true;
    }
    std::ofstream exp("/sys/class/gpio/export");
    if (exp.is_open()) {
        exp << kernel_gpio;
        exp.close();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool GpioManager::unexport_pin(int kernel_gpio) {
#ifndef _WIN32
    std::ofstream unexp("/sys/class/gpio/unexport");
    if (unexp.is_open()) {
        unexp << kernel_gpio;
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool GpioManager::set_direction(int kernel_gpio, PinMode mode) {
#ifndef _WIN32
    export_pin(kernel_gpio);
    std::string path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio) + "/direction";
    std::ofstream dir(path);
    if (dir.is_open()) {
        dir << (mode == PinMode::OUTPUT ? "out" : "in");
        return true;
    }
    return false;
#else
    return true;
#endif
}

bool GpioManager::set_value(int kernel_gpio, PinValue value) {
#ifndef _WIN32
    export_pin(kernel_gpio);
    std::string path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio) + "/value";
    std::ofstream val(path);
    if (val.is_open()) {
        val << (value == PinValue::HIGH ? "1" : "0");
        return true;
    }
    return false;
#else
    return true;
#endif
}

PinValue GpioManager::get_value(int kernel_gpio) {
#ifndef _WIN32
    export_pin(kernel_gpio);
    std::string path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio) + "/value";
    std::ifstream val(path);
    if (val.is_open()) {
        std::string s;
        val >> s;
        return (s == "1") ? PinValue::HIGH : PinValue::LOW;
    }
    return PinValue::LOW;
#else
    return PinValue::LOW;
#endif
}

// ── Public Core API ─────────────────────────────────────────────────────────

bool GpioManager::pin_mode(int physical_pin, PinMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    int k = physical_to_kernel_gpio(physical_pin);
    if (k < 0) return false;

    bool ok = set_direction(k, mode);
    pins_[physical_pin].mode = mode;
    pins_[physical_pin].exported = ok;
    return ok;
}

bool GpioManager::digital_write(int physical_pin, PinValue value) {
    std::lock_guard<std::mutex> lock(mutex_);
    int k = physical_to_kernel_gpio(physical_pin);
    if (k < 0) return false;

    bool ok = set_value(k, value);
    pins_[physical_pin].value = value;
    return ok;
}

PinValue GpioManager::digital_read(int physical_pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    int k = physical_to_kernel_gpio(physical_pin);
    if (k < 0) return PinValue::LOW;

    PinValue v = get_value(k);
    pins_[physical_pin].value = v;
    return v;
}

bool GpioManager::toggle(int physical_pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    int k = physical_to_kernel_gpio(physical_pin);
    if (k < 0) return false;

    PinValue current = pins_[physical_pin].value;
    PinValue next = (current == PinValue::HIGH) ? PinValue::LOW : PinValue::HIGH;
    bool ok = set_value(k, next);
    if (ok) {
        pins_[physical_pin].value = next;
    }
    return ok;
}

// ── Named Alias API ─────────────────────────────────────────────────────────

bool GpioManager::define_alias(const std::string& alias, int physical_pin) {
    std::lock_guard<std::mutex> lock(mutex_);
    aliases_[alias] = physical_pin;
    return true;
}

bool GpioManager::write_alias(const std::string& alias, PinValue value) {
    int pin = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) pin = it->second;
    }
    if (pin > 0) return digital_write(pin, value);
    return false;
}

PinValue GpioManager::read_alias(const std::string& alias) {
    int pin = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) pin = it->second;
    }
    if (pin > 0) return digital_read(pin);
    return PinValue::LOW;
}

// ── Non-blocking Access Control Helpers ─────────────────────────────────────

bool GpioManager::pulse(int physical_pin, int duration_ms, PinValue active_level) {
    // Execute pulse in a detached background thread so caller returns instantly
    std::thread([this, physical_pin, duration_ms, active_level]() {
        PinValue inactive_level = (active_level == PinValue::HIGH) ? PinValue::LOW : PinValue::HIGH;
        digital_write(physical_pin, active_level);
        std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        digital_write(physical_pin, inactive_level);
    }).detach();
    return true;
}

bool GpioManager::pulse_alias(const std::string& alias, int duration_ms, PinValue active_level) {
    int pin = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) pin = it->second;
    }
    if (pin > 0) return pulse(pin, duration_ms, active_level);
    return false;
}

bool GpioManager::buzzer_beep(int physical_pin, int count, int on_ms, int off_ms) {
    std::thread([this, physical_pin, count, on_ms, off_ms]() {
        for (int i = 0; i < count; ++i) {
            digital_write(physical_pin, PinValue::HIGH);
            std::this_thread::sleep_for(std::chrono::milliseconds(on_ms));
            digital_write(physical_pin, PinValue::LOW);
            if (i < count - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(off_ms));
            }
        }
    }).detach();
    return true;
}

bool GpioManager::buzzer_beep_alias(const std::string& alias, int count, int on_ms, int off_ms) {
    int pin = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = aliases_.find(alias);
        if (it != aliases_.end()) pin = it->second;
    }
    if (pin > 0) return buzzer_beep(pin, count, on_ms, off_ms);
    return false;
}

// ── Status & Inspection ─────────────────────────────────────────────────────

std::vector<PinInfo> GpioManager::get_all_pins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PinInfo> list;
    for (const auto& [p, info] : pins_) {
        PinInfo item = info;
        item.pin_number = item.physical_pin;
        item.alias = item.default_role.empty() ? item.name : item.default_role;
        item.state = item.value;
        list.push_back(item);
    }
    return list;
}

std::optional<PinInfo> GpioManager::get_pin_info(int physical_pin) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pins_.find(physical_pin);
    if (it != pins_.end()) return it->second;
    return std::nullopt;
}

json GpioManager::pins_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    json arr = json::array();
    for (const auto& [p, info] : pins_) {
        arr.push_back({
            {"physical_pin", info.physical_pin},
            {"kernel_gpio",  info.kernel_gpio},
            {"name",         info.name},
            {"role",         info.default_role},
            {"mode",         info.mode == PinMode::OUTPUT ? "output" : "input"},
            {"value",        static_cast<int>(info.value)}
        });
    }

    json alias_map = json::object();
    for (const auto& [a, p] : aliases_) {
        alias_map[a] = p;
    }

    return {
        {"pins", arr},
        {"aliases", alias_map}
    };
}

} // namespace app
