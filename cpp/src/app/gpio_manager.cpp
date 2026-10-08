#include "app/gpio_manager.hpp"
#include "platform/common/platform_detect.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <algorithm>

namespace app {

using json = nlohmann::json;

// ── Multi-Platform Physical Pin to Linux Kernel GPIO Mapping ─────────────────
// Dynamically maps 40-pin header pins on RK3588 (Orange Pi 5 Max) and Jetson Nano (J41)
int GpioManager::physical_to_kernel_gpio(int physical_pin) {
    int mapped = senzr::platform::map_logical_pin_to_sysfs(physical_pin);
    if (mapped > 0) return mapped;

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
}

// ── Convert RK3588 kernel GPIO to character device /dev/gpiochipN and line offset ──
std::pair<std::string, int> GpioManager::kernel_gpio_to_chip_line(int kernel_gpio) {
    if (kernel_gpio < 0) return {"", -1};
    int chip_idx = kernel_gpio / 32;
    int line_offset = kernel_gpio % 32;
    return {"/dev/gpiochip" + std::to_string(chip_idx), line_offset};
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
    define_alias("SAFETY_BEAM", 15);

    if (pins_.count(7))  pins_[7].default_role  = "RELAY";
    if (pins_.count(11)) pins_[11].default_role = "BUZZER";
    if (pins_.count(12)) pins_[12].default_role = "STATUS_LED";
    if (pins_.count(13)) pins_[13].default_role = "ERROR_LED";
    if (pins_.count(15)) {
        pins_[15].default_role = "DOOR_SENSOR";
        pins_[15].mode = PinMode::INPUT;
    }
}

GpioManager::~GpioManager() {
    stop_safety_monitor();
}

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
    std::string dir_path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio) + "/direction";
    std::string val_path = "/sys/class/gpio/gpio" + std::to_string(kernel_gpio) + "/value";

    // Ensure kernel GPIO direction is configured as output
    std::ifstream dir_in(dir_path);
    std::string cur_dir;
    if (!(dir_in >> cur_dir) || cur_dir != "out") {
        dir_in.close();
        std::ofstream dir_out(dir_path);
        if (dir_out.is_open()) {
            dir_out << "out";
        }
    }

    std::ofstream val(val_path);
    if (val.is_open()) {
        val << (value == PinValue::HIGH ? "1" : "0");
        return true;
    }
    
    // Fallback to libgpiod gpioset if sysfs is restricted or unavailable
    auto [chip_dev, line] = kernel_gpio_to_chip_line(kernel_gpio);
    if (!chip_dev.empty() && line >= 0) {
        std::string cmd = "gpioset " + chip_dev + " " + std::to_string(line) + "=" + (value == PinValue::HIGH ? "1" : "0") + " >/dev/null 2>&1";
        int rc = std::system(cmd.c_str());
        if (rc == 0) return true;
    }

    std::cerr << "[GPIO] Error: Could not set GPIO " << kernel_gpio 
              << " to " << (value == PinValue::HIGH ? "1" : "0")
              << " (sysfs / gpiod permission denied. Try running backend with sudo).\n";
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
    if (ok) {
        std::string alias = pins_[physical_pin].default_role.empty() ? ("Pin " + std::to_string(physical_pin)) : pins_[physical_pin].default_role;
        std::cout << "[GPIO] Physical Pin " << physical_pin << " (" << alias << ", Kernel GPIO " << k << ") -> " 
                  << (value == PinValue::HIGH ? "HIGH (1)" : "LOW (0)") << std::endl;
    }
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
    if (safety_tripped_.load()) {
        std::cerr << "[GpioManager] Cannot pulse pin " << physical_pin << ": safety interlock tripped!" << std::endl;
        return false;
    }

    auto abort_flag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(pulse_mutex_);
        active_pulse_flags_[physical_pin] = abort_flag.get();
    }

    // Execute pulse in a detached background thread so caller returns instantly
    std::thread([this, physical_pin, duration_ms, active_level, abort_flag]() {
        PinValue inactive_level = (active_level == PinValue::HIGH) ? PinValue::LOW : PinValue::HIGH;
        digital_write(physical_pin, active_level);

        auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < duration_ms) {
            if (abort_flag->load() || safety_tripped_.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        digital_write(physical_pin, inactive_level);

        std::lock_guard<std::mutex> lock(pulse_mutex_);
        if (active_pulse_flags_.count(physical_pin) && active_pulse_flags_[physical_pin] == abort_flag.get()) {
            active_pulse_flags_.erase(physical_pin);
        }
    }).detach();

    return true;
}

bool GpioManager::cancel_pulse(int physical_pin) {
    std::lock_guard<std::mutex> lock(pulse_mutex_);
    auto it = active_pulse_flags_.find(physical_pin);
    if (it != active_pulse_flags_.end() && it->second != nullptr) {
        it->second->store(true);
        digital_write(physical_pin, PinValue::LOW);
        return true;
    }
    return false;
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

// ── Safety Beam Interlock & Debounce (Module 6.2 & 6.3) ───────────────────

bool GpioManager::emergency_disarm(int relay_pin) {
    safety_tripped_.store(true);
    cancel_pulse(relay_pin);
    digital_write(relay_pin, PinValue::LOW);
    std::cerr << "[GpioManager] EMERGENCY DISARM TRIGGERED: Relay on Pin " << relay_pin << " disarmed." << std::endl;
    return true;
}

void GpioManager::start_safety_monitor(int safety_sensor_pin, int relay_pin) {
    if (safety_monitor_running_.load()) return;

    pin_mode(safety_sensor_pin, PinMode::INPUT);
    safety_monitor_running_.store(true);

    safety_thread_ = std::thread([this, safety_sensor_pin, relay_pin]() {
        while (safety_monitor_running_.load()) {
            // Safety sensors / opto-isolated beams are active LOW when broken
            PinValue state = digital_read_debounced(safety_sensor_pin, 20);
            if (state == PinValue::LOW) { // Beam interrupted
                if (!safety_tripped_.load()) {
                    emergency_disarm(relay_pin);
                }
            } else {
                safety_tripped_.store(false);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2)); // < 5ms disarm loop
        }
    });
}

void GpioManager::stop_safety_monitor() {
    if (safety_monitor_running_.load()) {
        safety_monitor_running_.store(false);
        if (safety_thread_.joinable()) {
            safety_thread_.join();
        }
    }
}

bool GpioManager::is_safety_tripped() const {
    return safety_tripped_.load();
}

PinValue GpioManager::digital_read_debounced(int physical_pin, int debounce_ms) {
    PinValue first_reading = digital_read(physical_pin);
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < debounce_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        PinValue check_reading = digital_read(physical_pin);
        if (check_reading != first_reading) {
            first_reading = check_reading;
            start = std::chrono::steady_clock::now(); // Reset debounce window on electrical chatter
        }
    }
    return first_reading;
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
            {"header_pin",   info.physical_pin},
            {"physical_pin", info.physical_pin},
            {"kernel_gpio",  info.kernel_gpio},
            {"name",         info.name},
            {"alias",        info.default_role.empty() ? info.name : info.default_role},
            {"role",         info.default_role.empty() ? info.name : info.default_role},
            {"direction",    info.mode == PinMode::OUTPUT ? "out" : "in"},
            {"mode",         info.mode == PinMode::OUTPUT ? "output" : "input"},
            {"level",        static_cast<int>(info.value)},
            {"value",        static_cast<int>(info.value)}
        });
    }

    json alias_map = json::object();
    for (const auto& [a, p] : aliases_) {
        alias_map[a] = p;
    }

    return {
        {"pins", arr},
        {"aliases", alias_map},
        {"safety_tripped", safety_tripped_.load()}
    };
}

} // namespace app
