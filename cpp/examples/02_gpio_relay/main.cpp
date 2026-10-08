#include <senzr/senzr.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== Example 02: GPIO Relay Pulsing & Safety Beam ===\n";

    auto& gpio = senzr::gpio::get_manager();

    // 1. Check all discovered pins
    auto pins = gpio.get_all_pins();
    std::cout << "Discovered " << pins.size() << " GPIO pins on RK3588 header.\n";

    // 2. Pulse door relay for 3000ms asynchronously
    std::cout << "Triggering 3-second non-blocking pulse on RELAY (Pin 7)...\n";
    auto res = gpio.pulse(7, 3000, senzr::gpio::PinValue::High);
    if (res) {
        std::cout << "Relay successfully energized!\n";
    } else {
        std::cerr << "GPIO Pulse Error: " << res.error().message() << "\n";
    }

    // 3. Inspect safety interlock
    if (gpio.is_safety_tripped()) {
        std::cout << "Warning: Safety beam is currently tripped!\n";
    } else {
        std::cout << "Safety beam clear. Barrier operational.\n";
    }

    return 0;
}
